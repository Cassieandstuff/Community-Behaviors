#include "PCH.h"

#include "CharDebugHud.h"
#include "CharFacing.h"
#include "CharGround.h"
#include "CharSweep.h"

#include <platform/hooks/Override.h>
#include <Physics/Utilities/CharacterControl/Bethesda/bhkCharProxyController.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <format>
#include <mutex>
#include <string>

/// @file
/// @brief Step 8 of the character controller spec: the trajectory predictor (P1/P2), observe-only. Every simulated step
///        of the player's proxy controller it predicts where the character will be 1/3, 2/3 and 1 s ahead (plus where it
///        was 1/3 s ago), and later scores each prediction against where the character actually went.
/// @note Groundwork for motion matching: the trajectory is the query feature. Nothing here writes to the controller.

namespace CB::feature {

    namespace {

        namespace hk = CB::havok;
        namespace ch = CB::feature::controller;
        using CheckSupport = platform::hooks::Override<&hk::bhkCharProxyController::CheckSupportImpl>;

        /// @brief Halflife of the velocity spring (P2), seconds.
        /// @warning Must match the halflife the ground states spring the real velocity with (CharStateWrapper's
        ///          kGroundHalflife), or the prediction models a different character than the one that runs.
        constexpr float kVelocityHalflife = 0.08f;
        using ch::kFacingHalflife;

        constexpr int   kFutureSamples = 3;
        constexpr float kSampleTimes[kFutureSamples] = { 1.0f / 3.0f, 2.0f / 3.0f, 1.0f };
        constexpr float kPastTime      = 1.0f / 3.0f;
        /// @brief The predicted path: a point every kPathStep seconds over the 1 s horizon, each resolved against the
        ///        world (floor, sweep, landing); the samples are among them (kSampleIndex).
        /// @note Six points, not more: each costs a sweep and two or three floor queries every physics step.
        constexpr int   kPathPoints                    = 6;
        constexpr float kPathStep                      = 1.0f / kPathPoints;
        constexpr int   kSampleIndex[kFutureSamples]   = { 1, 3, 5 };
        /// @brief The integration substep, seconds (60 per predicted second).
        constexpr int   kSubstepsPerPoint = 10;
        constexpr float kSubstep          = kPathStep / kSubstepsPerPoint;
        constexpr float kHalfPi           = 1.5707964f;
        /// @brief Rises smaller than this (game units/s) pass at once, as in the speed ramp.
        constexpr float kRampThreshold = 5.0f;
        /// @brief A vertical velocity this far above what the ground's slope gives (Havok units/s, about 35 game units/s)
        ///        means the character is leaving the ground (a jump's launch), not walking up it.
        constexpr float kLaunchSpeed = 0.5f;

        /// @brief Game units per Havok unit (Skyrim's world scale is 0.0142875); logs speak game units.
        constexpr float kGameUnitsPerHavok = 69.991251f;
        constexpr float kRadToDeg          = 57.29578f;
        /// @brief Below this speed (Havok units/s, about 7 game units/s) a character neither moves nor asks to.
        constexpr float kStillSpeed = 0.1f;
        /// @brief A sweep that starts closer than this to a body ignores that body (Havok's m_startPointTolerance).
        constexpr float kSweepStartTolerance = 0.005f;
        /// @brief How far a blocked sample backs off from the hit along its segment (Havok units, under 1 game unit).
        constexpr float kBlockBackoff = 0.01f;
        /// @brief Collision layers the sweep passes through: other characters (layer 30) are dynamic and move away.
        constexpr std::uint64_t kIgnoreLayers = 1ull << ch::kCharControllerLayer;

        /// @brief How often the gate reports and the full trajectory is dumped, in simulated seconds.
        constexpr double kReportPeriod = 5.0;
        constexpr double kDumpPeriod   = 3.0;
        /// @brief The predictor's whole log budget (lines), so a long session cannot flood the log.
        constexpr std::uint32_t kMaxLines = 4000;

        struct Vec2 {
            float x = 0, y = 0;
        };

        /// @brief Where the character was at a step, in Havok world space.
        struct HistoryEntry {
            double t = 0;
            float  x = 0, y = 0, z = 0;
            float  heading = 0;   ///< radians, atan2 of the movement frame's forward
        };

        /// @brief One sample of a trajectory, relative to the character's position and facing when it was predicted.
        struct Sample {
            float      time    = 0;   ///< seconds from now, negative for the past sample
            float      forward = 0, side = 0, up = 0;   ///< Havok units; side runs along the axis a sideways request moves the character (MovementAxes)
            float      facing  = 0;   ///< radians relative to the current facing
            ch::Ground ground  = ch::Ground::Airborne;
            bool       valid   = false;
        };

        /// @brief P1: the trajectory query.
        struct Trajectory {
            Sample                             past;
            std::array<Sample, kFutureSamples> future{};
            std::int32_t                       iState   = 0;   ///< held constant over the horizon (P1)
            float                              velForward = 0, velSide = 0;   ///< current velocity, Havok units/s
            float                              desiredForward = 0, desiredSide = 0;
            float                              blockedAt = -1.0f;   ///< seconds to the first impassable hit, -1 for none
        };

        /// @brief How the predicted character steers.
        /// @note The facing model is the frame features' own: the body turns on CharFacing's spring from where it is now
        ///       (yaw and turn rate) toward the stick, and moves along its facing at the frame speed, slowed by the turn
        ///       damping and rebuilt by the speed ramp. Without a stick direction (no TDM) the request is vanilla's,
        ///       held constant in world space.
        struct Drive {
            bool  facingModel = false;
            float theta = 0, omega = 0;   ///< facing as a math angle (counter-clockwise from +x) and its rate
            bool  haveGoal    = false;
            float thetaGoal   = 0;        ///< unwrapped near theta
            bool  turnDamping = false;    ///< the speed ramp slows turns (TurnSpeedScale)
            float full = 0, speed = 0;    ///< asked-for and current speed, Havok units/s
            float accel = 0;              ///< the ramp's rise rate, Havok units/s^2; 0 = instant
            Vec2  desired;                ///< the request, without the facing model
        };

        /// @brief The predicted path in Havok world space, for the HUD.
        struct Path {
            std::array<ch::hud::Point, kPathPoints> at{};
            std::array<ch::Ground, kPathPoints>     ground{};
            bool                                    blocked = false;
            ch::hud::Point                          blockedAt;
        };

        /// @brief A prediction waiting for the gate: its future samples in Havok world space.
        struct Pending {
            double                                  t0 = 0;
            std::array<Vec2, kFutureSamples>        at{};
            std::array<bool, kFutureSamples>        open{};
            bool                                    blocked = false;
        };

        struct HorizonStats {
            double sum = 0;
            float  max = 0;
            int    n   = 0;
        };

        /// @brief The predictor's whole state; the player is one controller, so one instance.
        struct State {
            const void*                    ctrl = nullptr;
            double                         clock = 0;
            std::array<HistoryEntry, 512>  history{};
            int                            historyHead = 0, historyCount = 0;
            std::array<Pending, 512>       pending{};
            int                            pendingHead = 0;
            std::array<HorizonStats, kFutureSamples> stats{}, blockedStats{};
            double                         lastReport = 0, lastDump = -1.0e9;
            std::int32_t                   lastIState = -1;
            bool                           lastBlocked = false;
        };

        std::mutex                 g_lock;
        State                      g_state;
        std::atomic<std::uint32_t> g_lines{ 0 };

        bool TakeLine() { return g_lines.fetch_add(1, std::memory_order_relaxed) < kMaxLines; }

        float WrapAngle(float a)
        {
            constexpr float kPi = 3.14159265f, kTwoPi = 6.2831853f;
            a = std::fmod(a + kPi, kTwoPi);
            if (a < 0) a += kTwoPi;
            return a - kPi;
        }

        /// @brief The horizontal frame vanilla's grounded states move the character in: where a forward request
        ///        (m_velocityMod y) and a sideways one (m_velocityMod x) move it, from calculateMovement itself.
        bool MovementFrame(const hk::bhkCharacterController& ctrl, Vec2& tangent, Vec2& side)
        {
            return ch::MovementAxes(ctrl, tangent.x, tangent.y, side.x, side.y);
        }

        void PushHistory(State& s, const HistoryEntry& e)
        {
            s.historyHead               = (s.historyHead + 1) % static_cast<int>(s.history.size());
            s.history[s.historyHead]    = e;
            s.historyCount              = (std::min)(s.historyCount + 1, static_cast<int>(s.history.size()));
        }

        /// @brief The history interpolated at time `t`; false when the history does not reach back that far.
        bool HistoryAt(const State& s, double t, HistoryEntry& out)
        {
            const int size = static_cast<int>(s.history.size());
            for (int i = 0; i + 1 < s.historyCount; i++) {
                const HistoryEntry& newer = s.history[(s.historyHead - i + size) % size];
                const HistoryEntry& older = s.history[(s.historyHead - i - 1 + size) % size];
                if (older.t > t) continue;
                const double span = newer.t - older.t;
                const float  a    = span > 1.0e-9 ? static_cast<float>((t - older.t) / span) : 1.0f;
                out.t       = t;
                out.x       = older.x + (newer.x - older.x) * a;
                out.y       = older.y + (newer.y - older.y) * a;
                out.z       = older.z + (newer.z - older.z) * a;
                out.heading = older.heading + WrapAngle(newer.heading - older.heading) * a;
                return true;
            }
            return false;
        }

        void Reset(State& s, const void* ctrl)
        {
            s       = State{};
            s.ctrl  = ctrl;
        }

        /// @brief How far the floor rises along a leg (dx, dy) if it keeps the plane of `v`'s ground normal (Havok units).
        /// @note Clamped to a 60 deg grade: a near-vertical normal would extrapolate the leg into a cliff.
        float PlaneRise(const ch::FloorVerdict& v, float dx, float dy)
        {
            if (v.ground == ch::Ground::Airborne || v.groundNz < 0.1f) return 0.0f;
            const float rise  = -(v.groundNx * dx + v.groundNy * dy) / v.groundNz;
            const float limit = 1.7320508f * std::sqrt(dx * dx + dy * dy);
            return std::clamp(rise, -limit, limit);
        }

        /// @brief The highest floor-like hit (walls and characters excluded) above or below the query's feet, or none.
        bool HighestFloor(const ch::FloorVerdict& v, float& dz)
        {
            const ch::FloorTunables tun;
            bool found = false;
            for (const ch::FloorHit& h : v.hits) {
                if (!h.hit || h.layer == ch::kCharControllerLayer || h.nz < tun.wallNormalZ) continue;
                dz    = found ? (std::max)(dz, h.dz) : h.dz;
                found = true;
            }
            return found;
        }

        /// @brief The floor under a predicted sample at (x, y), and the height `z` its feet settle at.
        /// @note Two passes. The first queries at `zEstimate` (the previous sample's floor plane carried along the leg),
        ///       falling back to `zPrevious` when that finds nothing; the sample settles onto the highest floor-like
        ///       hit, and the second query, from the settled height, gives the verdict. Querying at the previous
        ///       sample's height instead put the rays' start under an uphill slope (a few edge hits averaging steep: a
        ///       false Sliding) or their reach above a downhill one (a false Airborne).
        ch::FloorVerdict SettleFloor(hk::bhkCharacterController& ctrl, const ch::Hull& hull, float x, float y, float zEstimate, float zPrevious,
                                     float& z)
        {
            float            dz    = 0.0f;
            ch::FloorVerdict first = ch::QueryFloorAt(ctrl, hull, x, y, zEstimate);
            z                      = zEstimate;
            if (!HighestFloor(first, dz)) {
                first = ch::QueryFloorAt(ctrl, hull, x, y, zPrevious);
                z     = zPrevious;
                if (!HighestFloor(first, dz)) return first;
            }
            z += dz;
            return ch::QueryFloorAt(ctrl, hull, x, y, z);
        }

        /// @brief Where a fall from `zTop` to `zBottom` (feet heights) at (x, y) lands: the highest floor-like hit between
        ///        them, probed with the floor query stepped down the drop (its rays reach one hull radius above the feet
        ///        and the step height below).
        bool Land(hk::bhkCharacterController& ctrl, const ch::Hull& hull, float x, float y, float zTop, float zBottom, float& zLand,
                  ch::FloorVerdict& floor)
        {
            const float span = hull.radius + ctrl.m_stepHeight;
            float       zq   = zTop - hull.radius;
            for (int i = 0; i < 4 && zq + hull.radius >= zBottom; i++, zq -= span) {
                float dz = 0.0f;
                if (!HighestFloor(ch::QueryFloorAt(ctrl, hull, x, y, zq), dz)) continue;
                const float hit = zq + dz;
                if (hit > zTop || hit < zBottom) continue;
                zLand = hit;
                floor = ch::QueryFloorAt(ctrl, hull, x, y, zLand);
                return true;
            }
            return false;
        }

        /// @brief P2: one forward simulation of the Drive (facing spring, speed, a critically damped velocity spring on
        ///        the ground, gravity in the air), resolved against the world at every path point: the floor under it,
        ///        a sweep along the leg, and where a fall lands.
        /// @note Vertical: the character starts airborne when the floor query finds no floor or it is rising faster
        ///       than the ground's slope gives (a jump). A grounded point with no floor within reach walks off a ledge:
        ///       the leg falls from its start. In the air, gravity is 1x rising and fInAirFallingCharGravityMult:HAVOK
        ///       falling (the Airborne state's), a held jump is assumed, and the horizontal velocity holds (air control
        ///       is weak and not modelled).
        /// @param worldOut the future samples' positions in Havok world space, for the gate.
        Trajectory Predict(State& s, hk::bhkCharacterController& ctrl, const ch::Hull& hull, const Vec2& tangent, const Vec2& sideAxis,
                           const hk::hkVector4& velocity, const Drive& drive, std::int32_t iState, std::array<Vec2, kFutureSamples>& worldOut,
                           Path& path)
        {
            Trajectory tr;
            tr.iState     = iState;
            tr.velForward = velocity(0) * tangent.x + velocity(1) * tangent.y;
            tr.velSide    = velocity(0) * sideAxis.x + velocity(1) * sideAxis.y;
            Vec2 want     = drive.desired;
            if (drive.facingModel) {
                const float a = drive.haveGoal ? drive.thetaGoal : drive.theta;
                want          = { drive.full * std::cos(a), drive.full * std::sin(a) };
            }
            tr.desiredForward   = want.x * tangent.x + want.y * tangent.y;
            tr.desiredSide      = want.x * sideAxis.x + want.y * sideAxis.y;
            const float heading = std::atan2(tangent.y, tangent.x);

            auto toLocal = [&](float dx, float dy, Sample& out) {
                out.forward = dx * tangent.x + dy * tangent.y;
                out.side    = dx * sideAxis.x + dy * sideAxis.y;
            };

            HistoryEntry past;
            if (HistoryAt(s, s.clock - kPastTime, past)) {
                tr.past.time  = -kPastTime;
                toLocal(past.x - hull.cx, past.y - hull.cy, tr.past);
                tr.past.up     = past.z - hull.feetZ;
                tr.past.facing = WrapAngle(past.heading - heading);
                tr.past.ground = ch::QueryFloorAt(ctrl, hull, past.x - hull.cx, past.y - hull.cy, tr.past.up).ground;
                tr.past.valid  = true;
            }

            /// @note Without the facing model the facing goal is the request's direction while there is one, else the
            ///       current facing.
            const float wantSpeed  = std::sqrt(want.x * want.x + want.y * want.y);
            const float facingGoal = drive.facingModel ? (drive.haveGoal ? drive.thetaGoal : drive.theta)
                                                       : wantSpeed > kStillSpeed ? heading + WrapAngle(std::atan2(want.y, want.x) - heading)
                                                                                 : heading;
            const float threshold  = kRampThreshold / kGameUnitsPerHavok;

            const hk::hkVector4& gw       = hull.world->GetGravity();
            const float          gravity  = std::sqrt(gw(0) * gw(0) + gw(1) * gw(1) + gw(2) * gw(2)) * ctrl.m_gravity;
            static const float   fallMult = ch::IniFloat("fInAirFallingCharGravityMult:HAVOK", 1.35f);

            /// @note The sweep runs lifted by the step height, so what the proxy steps over (stair risers, kerbs) never
            ///       blocks; walkable faces are skipped (m_zNormal) and other characters too. Each grounded point's height
            ///       follows the floor query, so stairs and slopes keep the next leg at the floor.
            const hk::hkVector4& origin    = hull.collidable->getTransform().getTranslation();
            const float          lift      = ctrl.m_stepHeight;
            ch::FloorVerdict     prevFloor = ch::QueryFloor(ctrl, hull);
            bool airborne = prevFloor.ground == ch::Ground::Airborne || velocity(2) - PlaneRise(prevFloor, velocity(0), velocity(1)) > kLaunchSpeed;

            float f = drive.facingModel ? drive.theta : heading, fv = drive.facingModel ? drive.omega : 0.0f;
            float spd = drive.speed;
            float vx = velocity(0), vy = velocity(1), vz = airborne ? velocity(2) : 0.0f, ax = 0, ay = 0;
            float px = 0, py = 0, z = 0;
            Vec2  prev{};
            float prevZ = 0, prevT = 0;
            bool  blocked = false;
            int   sample  = 0;
            for (int i = 0; i < kPathPoints; i++) {
                for (int k = 0; k < kSubstepsPerPoint; k++) {
                    const float h = kSubstep;
                    ch::SpringDamperExact(f, fv, facingGoal, kFacingHalflife, h);
                    Vec2 d = want;
                    if (drive.facingModel) {
                        const float target = drive.full * (drive.turnDamping && drive.haveGoal ? ch::TurnSpeedScale(facingGoal - f) : 1.0f);
                        spd = drive.accel > 0.0f && target > spd + threshold ? (std::min)(target, spd + drive.accel * h) : target;
                        d   = { spd * std::cos(f), spd * std::sin(f) };
                    }
                    const float ox = vx, oy = vy, oz = vz;
                    if (!airborne) {
                        ch::SpringDamperExact(vx, ax, d.x, kVelocityHalflife, h);
                        ch::SpringDamperExact(vy, ay, d.y, kVelocityHalflife, h);
                    } else {
                        vz -= gravity * (vz > 0.0f ? 1.0f : fallMult) * h;
                        z += (oz + vz) * 0.5f * h;
                    }
                    if (!blocked) {
                        px += (ox + vx) * 0.5f * h;
                        py += (oy + vy) * 0.5f * h;
                    }
                }

                Vec2             at{ px, py };
                ch::FloorVerdict floor;
                if (!airborne) {
                    floor = SettleFloor(ctrl, hull, at.x, at.y, prevZ + PlaneRise(prevFloor, at.x - prev.x, at.y - prev.y), prevZ, z);
                    if (floor.ground == ch::Ground::Airborne) {
                        airborne = true;
                        vz       = -gravity * fallMult * kPathStep;
                        z        = prevZ - 0.5f * gravity * fallMult * kPathStep * kPathStep;
                    }
                }
                if (airborne) {
                    float zLand = 0.0f;
                    if (vz <= 0.0f && Land(ctrl, hull, at.x, at.y, prevZ, z, zLand, floor)) {
                        z        = zLand;
                        vz       = 0.0f;
                        airborne = false;
                    } else {
                        floor = ch::FloorVerdict{};
                    }
                }
                if (!blocked) {
                    hk::hkVector4 from, to;
                    from.set(origin(0) + prev.x, origin(1) + prev.y, origin(2) + prevZ + lift, 0.0f);
                    to.set(origin(0) + at.x, origin(1) + at.y, origin(2) + z + lift, 0.0f);
                    const ch::SweepHit hit = ch::SweepFrom(hull.world, hull.collidable, from, to, kSweepStartTolerance, ctrl.m_zNormal, kIgnoreLayers);
                    if (hit.hit) {
                        const float dx = at.x - prev.x, dy = at.y - prev.y;
                        const float len = std::sqrt(dx * dx + dy * dy);
                        const float b   = (std::max)(0.0f, hit.fraction - (len > 1.0e-6f ? kBlockBackoff / len : 0.0f));
                        blocked         = true;
                        tr.blockedAt    = prevT + kPathStep * hit.fraction;
                        at              = { prev.x + dx * b, prev.y + dy * b };
                        if (!airborne) floor = SettleFloor(ctrl, hull, at.x, at.y, prevZ + PlaneRise(prevFloor, at.x - prev.x, at.y - prev.y), prevZ, z);
                        else           z = prevZ + (z - prevZ) * b;
                        px = at.x, py = at.y;
                        vx = vy = ax = ay = 0.0f;
                        path.blocked   = true;
                        path.blockedAt = { hull.cx + at.x, hull.cy + at.y, hull.feetZ + z };
                    }
                }
                if (floor.ground != ch::Ground::Airborne) prevFloor = floor;
                path.at[i]     = { hull.cx + at.x, hull.cy + at.y, hull.feetZ + z };
                path.ground[i] = floor.ground;
                if (sample < kFutureSamples && i == kSampleIndex[sample]) {
                    Sample& out = tr.future[sample];
                    out.time    = kSampleTimes[sample];
                    toLocal(at.x, at.y, out);
                    out.up           = z;
                    out.facing       = WrapAngle(f - heading);
                    out.ground       = floor.ground;
                    out.valid        = true;
                    worldOut[sample] = { hull.cx + at.x, hull.cy + at.y };
                    ++sample;
                }
                prev  = at;
                prevZ = z;
                prevT = (i + 1) * kPathStep;
            }
            return tr;
        }

        void Dump(const State& s, const Trajectory& tr, const char* why)
        {
            if (!TakeLine()) return;
            auto fmtSample = [](const Sample& p) {
                if (!p.valid) return std::string("n/a");
                return std::format("({:.0f} fwd, {:.0f} side, {:.0f} up, {:.0f} deg, {})", p.forward * kGameUnitsPerHavok,
                                   p.side * kGameUnitsPerHavok, p.up * kGameUnitsPerHavok, p.facing * kRadToDeg, ch::Name(p.ground));
            };
            LOG_INFO("predictor: trajectory ({}) t {:.2f} iState {} vel ({:.0f} fwd, {:.0f} side) desired ({:.0f} fwd, {:.0f} side) | -1/3 s {} | "
                     "1/3 s {} | 2/3 s {} | 1 s {} | blocked {}",
                     why, s.clock, tr.iState, tr.velForward * kGameUnitsPerHavok, tr.velSide * kGameUnitsPerHavok,
                     tr.desiredForward * kGameUnitsPerHavok, tr.desiredSide * kGameUnitsPerHavok, fmtSample(tr.past), fmtSample(tr.future[0]),
                     fmtSample(tr.future[1]), fmtSample(tr.future[2]),
                     tr.blockedAt < 0 ? std::string("no") : std::format("at {:.2f} s", tr.blockedAt));
        }

        /// @brief Scores every pending prediction whose sample time fell inside the last step, against the actual
        ///        position interpolated to that instant.
        void Score(State& s, const HistoryEntry& before, const HistoryEntry& now)
        {
            const double span = now.t - before.t;
            for (Pending& p : s.pending) {
                for (int k = 0; k < kFutureSamples; k++) {
                    if (!p.open[k]) continue;
                    const double due = p.t0 + kSampleTimes[k];
                    if (due > now.t) continue;
                    p.open[k] = false;
                    if (due < before.t) continue;
                    const float a  = span > 1.0e-9 ? static_cast<float>((due - before.t) / span) : 1.0f;
                    const float ax = before.x + (now.x - before.x) * a, ay = before.y + (now.y - before.y) * a;
                    const float dx = ax - p.at[k].x, dy = ay - p.at[k].y;
                    const float e  = std::sqrt(dx * dx + dy * dy) * kGameUnitsPerHavok;
                    HorizonStats& st = (p.blocked ? s.blockedStats : s.stats)[k];
                    st.sum += e;
                    st.max = (std::max)(st.max, e);
                    ++st.n;
                }
            }
        }

        void Report(State& s)
        {
            if (s.clock - s.lastReport < kReportPeriod) return;
            const double window = s.clock - s.lastReport;
            s.lastReport        = s.clock;
            auto line = [](const std::array<HorizonStats, kFutureSamples>& st) {
                std::string out;
                for (int k = 0; k < kFutureSamples; k++) {
                    static constexpr const char* kNames[kFutureSamples] = { "1/3 s", "2/3 s", "1 s" };
                    out += st[k].n ? std::format("{}{} mean {:.1f} max {:.1f} (n {})", k ? " | " : "", kNames[k], st[k].sum / st[k].n, st[k].max, st[k].n)
                                   : std::format("{}{} n 0", k ? " | " : "", kNames[k]);
                }
                return out;
            };
            const bool any = s.stats[0].n || s.blockedStats[0].n || s.stats[2].n || s.blockedStats[2].n;
            if (any && TakeLine())
                LOG_INFO("predictor: error over {:.1f} s (game units) free: {} || blocked: {}", window, line(s.stats), line(s.blockedStats));
            s.stats.fill({});
            s.blockedStats.fill({});
        }

        /// @brief Observes the step after every other CheckSupportImpl subscriber (Priority::First is outermost).
        /// @note Why CheckSupportImpl rather than Unk_0C: it runs once per step after the step's movement request
        ///       (m_velocityMod) and support are final but before the state simulates, so the prediction starts from
        ///       exactly the inputs the state is about to read, and it is inside UpdatePhysics, where the floor query and
        ///       the sweep may query the world.
        void OnCheckSupport(CheckSupport::Next next, hk::bhkCharacterController* self)
        {
            next(self);
            if (!self || !ch::IsManaged(*self)) return;
            hk::bhkCharacterController& ctrl = *self;
            const float dt = ctrl.m_stepInfo.m_deltaTime;
            const ch::Hull hull = ch::GetHull(ctrl);
            Vec2 tangent, sideAxis;
            if (!hull.valid || !MovementFrame(ctrl, tangent, sideAxis)) return;

            std::scoped_lock lock(g_lock);
            State& s = g_state;
            if (s.ctrl != self) Reset(s, self);
            const HistoryEntry here{ s.clock + (dt > 0 ? dt : 0), hull.cx, hull.cy, hull.feetZ, std::atan2(tangent.y, tangent.x) };
            if (s.historyCount) {
                const HistoryEntry& last = s.history[s.historyHead];
                const float jx = here.x - last.x, jy = here.y - last.y;
                /// @note A step that is not a step (a load, a pause, a teleport) restarts the history and the gate.
                if (!(dt > 0.0f && dt < 0.25f) || jx * jx + jy * jy > 4.0f) {
                    Reset(s, self);
                    s.clock = 0;
                    PushHistory(s, { 0, here.x, here.y, here.z, here.heading });
                    return;
                }
            }
            s.clock = here.t;
            if (s.historyCount) Score(s, s.history[s.historyHead], here);
            PushHistory(s, here);

            hk::hkVector4 v;
            self->GetLinearVelocityImpl(v);
            const float vmx = ctrl.m_velocityMod(0), vmy = ctrl.m_velocityMod(1);
            const Vec2  desired{ vmy * tangent.x + vmx * sideAxis.x, vmy * tangent.y + vmx * sideAxis.y };
            const float asked = std::sqrt(desired.x * desired.x + desired.y * desired.y);

            /// @note Skyrim headings run clockwise from +y; the math angle is pi/2 - heading.
            Drive drive;
            drive.desired = desired;
            drive.full = drive.speed = asked;
            ch::FacingState facing;
            float           stick = 0.0f;
            if (ch::CurrentFacing(facing)) {
                drive.facingModel = true;
                drive.theta       = kHalfPi - facing.yaw;
                drive.omega       = -facing.rate;
                drive.haveGoal    = facing.haveGoal;
                if (facing.haveGoal) drive.thetaGoal = drive.theta + WrapAngle(kHalfPi - facing.goal - drive.theta);
                /// @note The frame speed is game units/s; the request it becomes (m_velocityMod) is Havok units/s.
                if (ch::FrameSpeed fs; ch::CurrentSpeed(fs)) {
                    drive.turnDamping = true;
                    drive.full        = fs.full / kGameUnitsPerHavok;
                    drive.speed       = fs.speed / kGameUnitsPerHavok;
                    drive.accel       = fs.rampTime > 0.0f ? drive.full / fs.rampTime : 0.0f;
                }
            } else if (ch::StickHeading(stick)) {
                /// @note TDM turning the body itself (bFacing off): its forward-only request goes where the stick points,
                ///       with CB's facing spring from the body's facing standing in for TDM's turn rate.
                drive.facingModel = true;
                drive.theta       = std::atan2(tangent.y, tangent.x);
                drive.haveGoal    = true;
                drive.thetaGoal   = drive.theta + WrapAngle(kHalfPi - stick - drive.theta);
            }

            std::int32_t iState = 0;
            if (auto* player = RE::PlayerCharacter::GetSingleton()) {
                static const RE::BSFixedString kIState("iState");
                player->GetGraphVariableInt(kIState, iState);
            }

            const float speed  = std::sqrt(v(0) * v(0) + v(1) * v(1));
            const bool  active = speed > kStillSpeed || asked > kStillSpeed || (drive.facingModel && drive.full > kStillSpeed);

            std::array<Vec2, kFutureSamples> world{};
            Path                             path;
            const Trajectory tr = Predict(s, ctrl, hull, tangent, sideAxis, v, drive, iState, world, path);
            const bool blocked  = tr.blockedAt >= 0.0f;
            if (ch::hud::On()) {
                ch::hud::Prediction h;
                for (int k = 0; k < kFutureSamples && k < ch::hud::Prediction::kSamples; k++) {
                    h.at[k]     = { world[k].x, world[k].y, hull.feetZ + tr.future[k].up };
                    h.ground[k] = tr.future[k].ground;
                    h.time[k]   = kSampleTimes[k];
                }
                static_assert(kPathPoints <= ch::hud::Prediction::kPath);
                for (int i = 0; i < kPathPoints; i++) {
                    h.path[i]       = path.at[i];
                    h.pathGround[i] = path.ground[i];
                }
                h.pathCount = kPathPoints;
                h.blocked   = path.blocked;
                h.blockedAt = path.blockedAt;
                h.blockedIn = tr.blockedAt;
                h.iState    = iState;
                ch::hud::Publish(h);
            }

            if (active) {
                s.pendingHead = (s.pendingHead + 1) % static_cast<int>(s.pending.size());
                Pending& p    = s.pending[s.pendingHead];
                p.t0          = s.clock;
                p.at          = world;
                p.open.fill(true);
                p.blocked     = blocked;
            }

            const char* why = nullptr;
            if (iState != s.lastIState && s.lastIState != -1) why = "iState changed";
            else if (blocked && !s.lastBlocked) why = "blocked";
            else if (active && s.clock - s.lastDump >= kDumpPeriod) why = "periodic";
            if (why) {
                Dump(s, tr, why);
                s.lastDump = s.clock;
            }
            s.lastIState  = iState;
            s.lastBlocked = blocked;
            Report(s);
        }

        [[maybe_unused]] const bool s_predictor =
            CheckSupport::Subscribe(&OnCheckSupport, platform::hooks::Priority::First, "CharPredictor", "char_predictor");

    }  // namespace

}  // namespace CB::feature
