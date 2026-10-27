#include "PCH.h"

#include "CharMovementModel.h"

#include "CharGround.h"

#include <core/debug/Settings.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <mutex>
#include <unordered_map>

/// @file
/// @brief The movement model's step: Unreal's UCharacterMovementComponent::CalcVelocity and ApplyVelocityBraking for
///        walking, with GASP's per-tick numbers (CharMovementModel.h), and the controller-owned facing spring.

namespace CB::feature::controller::move {

    namespace {

        constexpr float kPi  = 3.14159265f;
        constexpr float kDeg = kPi / 180.0f;

        float MapRangeClamped(float x, float inA, float inB, float outA, float outB)
        {
            const float t = std::clamp((x - inA) / (inB - inA), 0.0f, 1.0f);
            return outA + (outB - outA) * t;
        }

        /// @brief CMC's ApplyVelocityBraking.
        /// @note  The deceleration's direction is fixed at the start of the step and a substep that would reverse the
        ///        velocity stops it instead, so braking never pushes backward.
        void Brake(float& vx, float& vy, float friction, float frictionFactor, float deceleration, float dt)
        {
            const float len = std::hypot(vx, vy);
            if (len <= 0.0f || dt < 1.0e-6f) return;
            friction                = (std::max)(0.0f, friction * frictionFactor);
            const bool  zeroFriction = friction == 0.0f, zeroBraking = deceleration == 0.0f;
            if (zeroFriction && zeroBraking) return;
            const float ox = vx, oy = vy;
            const float rx = zeroBraking ? 0.0f : -deceleration * vx / len, ry = zeroBraking ? 0.0f : -deceleration * vy / len;
            float       remaining = dt;
            while (remaining >= 1.0e-6f) {
                const float step = remaining > kBrakingSubstep && !zeroFriction ? (std::min)(kBrakingSubstep, remaining * 0.5f) : remaining;
                remaining -= step;
                vx += (-friction * vx + rx) * step;
                vy += (-friction * vy + ry) * step;
                if (vx * ox + vy * oy <= 0.0f) {
                    vx = vy = 0.0f;
                    return;
                }
            }
            const float sq = vx * vx + vy * vy;
            if (sq <= 1.0e-4f || (!zeroBraking && sq <= kBrakeToStop * kBrakeToStop)) vx = vy = 0.0f;
        }

        bool ExceedsMax(float sq, float maxSpeed) { return sq > maxSpeed * maxSpeed * kOverMaxTolerance; }

        /// @brief CMC's CalcVelocity for walking.
        void Velocity(State& s, const Input& in, const Profile& p, float maxSpeed, float dt)
        {
            const float speed    = std::hypot(s.vx, s.vy);
            const bool  sprint   = in.gait == Gait::Sprint;
            const float accel    = sprint ? MapRangeClamped(speed, p.sprintAccelFrom, p.sprintAccelTo, p.accel, p.sprintAccelSlow) : p.accel;
            const float friction = sprint ? MapRangeClamped(speed, 0.0f, p.sprintFrictionTo, p.friction, p.sprintFrictionLow) : p.friction;
            const bool  push     = in.haveStick;
            const float ax = push ? in.stickX * accel : 0.0f, ay = push ? in.stickY * accel : 0.0f;
            const bool  overMax = ExceedsMax(speed * speed, maxSpeed);
            if (!push || overMax) {
                const float ox = s.vx, oy = s.vy;
                Brake(s.vx, s.vy, friction, p.brakingFrictionFactor, push ? p.brakingWithInput : p.brakingNoInput, dt);
                /// @note Braking from above the cap stops at the cap while the stick still pushes forward (a gait drop
                ///       eases to the new speed instead of braking through it).
                if (overMax && s.vx * s.vx + s.vy * s.vy < maxSpeed * maxSpeed && ax * ox + ay * oy > 0.0f && speed > 0.0f)
                    s.vx = ox / speed * maxSpeed, s.vy = oy / speed * maxSpeed;
            } else {
                /// @note Friction swings the velocity toward the stick at its current speed: the turn GASP's friction 5
                ///       makes, with no explicit speed loss.
                const float k = (std::min)(dt * friction, 1.0f);
                s.vx -= (s.vx - in.stickX * speed) * k;
                s.vy -= (s.vy - in.stickY * speed) * k;
            }
            if (!push) return;
            const float now      = std::hypot(s.vx, s.vy);
            const float maxInput = ExceedsMax(now * now, maxSpeed) ? now : maxSpeed;
            s.vx += ax * dt;
            s.vy += ay * dt;
            const float after = std::hypot(s.vx, s.vy);
            if (after > maxInput && after > 0.0f) s.vx *= maxInput / after, s.vy *= maxInput / after;
        }

        /// @brief The profile's traction on the ground step that turned the velocity (v0x, v0y) into s's: the change is split
        ///        along the old velocity and across it; dig-in braking joins the part along it, the slowing is held to
        ///        brakingGrip and the part across to lateralGrip.
        /// @return Whether the horse digs in this step (the stick past digInAngle from a moving velocity).
        /// @note  The braking cap never lets the velocity pass through zero within the step: a dig slides to a stop and
        ///        turns on the hocks once slower than facingMinSpeed (where traction no longer applies).
        bool Traction(State& s, const Input& in, const Profile& p, float v0x, float v0y, float dt)
        {
            const float speed0 = std::hypot(v0x, v0y);
            if (p.lateralGrip <= 0.0f || speed0 < p.facingMinSpeed) return false;
            const float ux = v0x / speed0, uy = v0y / speed0;
            const float dvx = s.vx - v0x, dvy = s.vy - v0y;
            float       along  = dvx * ux + dvy * uy;
            float       across = dvx * -uy + dvy * ux;

            bool dig = false;
            if (in.haveStick && p.digInBraking > 0.0f) {
                const float angle = std::acos(std::clamp(in.stickX * ux + in.stickY * uy, -1.0f, 1.0f));
                if (angle > p.digInAngle) {
                    dig              = true;
                    const float ramp = (angle - p.digInAngle) / (std::max)(1.0e-3f, kPi - p.digInAngle);
                    along -= p.digInBraking * ramp * dt;
                }
            }
            if (p.brakingGrip > 0.0f) along = (std::max)(along, -(std::min)(speed0, p.brakingGrip * dt));
            else along = (std::max)(along, -speed0);

            const float gripAccel = p.minTurnRadius > 0.0f ? (std::min)(p.lateralGrip, speed0 * speed0 / p.minTurnRadius) : p.lateralGrip;
            across                = std::clamp(across, -gripAccel * dt, gripAccel * dt);
            s.vx                  = v0x + along * ux - across * uy;
            s.vy                  = v0y + along * uy + across * ux;
            return dig;
        }

        void Facing(State& s, const Input& in, const Profile& p, bool dig, float px, float py, float dt)
        {
            float goal     = s.yaw;
            bool  have     = false;
            float halflife = p.facingHalflife;
            if (in.mode == Mode::Strafe && in.haveTarget) {
                const float dx = in.targetX - px, dy = in.targetY - py;
                if (dx * dx + dy * dy > 1.0f) goal = std::atan2(dx, dy), have = true;
            }
            /// @note In a dig the body leads: it turns toward the stick, at most digInMaxLead past the velocity, while the
            ///       velocity lags on the grip.
            if (!have && dig && p.digInFacingHalflife > 0.0f) {
                goal = std::atan2(in.stickX, in.stickY);
                if (p.digInMaxLead > 0.0f) {
                    const float vel = std::atan2(s.vx, s.vy);
                    goal            = vel + std::clamp(WrapPi(goal - vel), -p.digInMaxLead, p.digInMaxLead);
                }
                have     = true;
                halflife = p.digInFacingHalflife;
            }
            if (!have && std::hypot(s.vx, s.vy) >= p.facingMinSpeed) goal = std::atan2(s.vx, s.vy), have = true;
            if (have) goal = s.yaw + WrapPi(goal - s.yaw);
            const float before = s.yaw;
            SpringDamperExact(s.yaw, s.yawRate, goal, halflife, dt);
            if (!in.airborne) return;
            const float limit = p.airTurnRate * dt;
            s.yaw             = before + std::clamp(s.yaw - before, -limit, limit);
            s.yawRate         = std::clamp(s.yawRate, -p.airTurnRate, p.airTurnRate);
        }

        struct Published {
            Snapshot snap;
            double   at = -1.0e9;
        };
        /// @brief Every driven actor's latest snapshot, by FormID.
        /// @note Never pruned: only the actors a frame driver holds publish (the player, a ridden horse), a handful.
        struct PublishedTable {
            std::mutex                                 lock;
            std::unordered_map<RE::FormID, Published> byActor;
        };
        PublishedTable   g_published;
        constexpr double kStale = 0.5;

        double Now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

    }  // namespace

    float StrafeSpeedMap(float angle)
    {
        const float d = std::fabs(angle) / kDeg;
        if (d <= 45.0f) return 0.0f;
        if (d < 80.0f) return (d - 45.0f) / 35.0f;
        if (d <= 100.0f) return 1.0f;
        if (d < 135.0f) return 1.0f + (d - 100.0f) / 35.0f;
        return 2.0f;
    }

    float MaxSpeed(const Caps& caps, Mode mode, float velYaw, float yaw)
    {
        if (mode == Mode::Orient) return caps.forward;
        const float rel  = WrapPi(velYaw - yaw);
        const float m    = StrafeSpeedMap(rel);
        const float side = rel >= 0.0f ? caps.right : caps.left;
        const float r    = m < 1.0f ? 1.0f + (side - 1.0f) * m : side + (caps.back - side) * (m - 1.0f);
        return caps.forward * r;
    }

    void Step(State& s, const Input& in, const Caps& caps, const Profile& profile, float px, float py, float dt)
    {
        if (dt <= 0.0f) return;
        bool dig = false;
        if (!in.airborne) {
            const float velYaw = std::hypot(s.vx, s.vy) > 1.0f ? std::atan2(s.vx, s.vy) : s.yaw;
            const float v0x = s.vx, v0y = s.vy;
            Velocity(s, in, profile, MaxSpeed(caps, in.mode, velYaw, s.yaw), dt);
            dig = Traction(s, in, profile, v0x, v0y, dt);
        }
        Facing(s, in, profile, dig, px, py, dt);
    }

    bool SprintDirectionAllowed(const State& s, const Input& in, const Profile& profile)
    {
        if (in.mode == Mode::Orient || !in.haveStick) return true;
        return std::fabs(WrapPi(std::atan2(in.stickX, in.stickY) - s.yaw)) < profile.sprintCone;
    }

    bool Current(RE::FormID actor, Snapshot& out)
    {
        std::scoped_lock lock(g_published.lock);
        const auto       it = g_published.byActor.find(actor);
        if (it == g_published.byActor.end() || Now() - it->second.at > kStale) return false;
        out = it->second.snap;
        return true;
    }

    bool Current(Snapshot& out) { return Current(kPlayerId, out); }

    void Publish(RE::FormID actor, const Snapshot& snap)
    {
        std::scoped_lock lock(g_published.lock);
        g_published.byActor[actor] = Published{ snap, Now() };
    }

    bool On()
    {
        static const bool on = core::debug::FlagOn("char_movement_model");
        return on;
    }

}  // namespace CB::feature::controller::move
