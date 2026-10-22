#include "PCH.h"

#include "CharDebugHud.h"
#include "CharFacing.h"
#include "CharGround.h"
#include "CharMotionMatching.h"
#include "CharMovementModel.h"
#include "CharSweep.h"

#include <compile/MotionFeatures.h>
#include <platform/hooks/Live.h>
#include <platform/hooks/Override.h>
#include <platform/profile/Profile.h>
#include <Physics/Utilities/CharacterControl/Bethesda/bhkCharProxyController.h>
#include <Physics/Utilities/CharacterControl/Bethesda/bhkCollisionFilter.h>

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

        constexpr int   kFutureSamples = 3;
        constexpr float kSampleTimes[kFutureSamples] = { 1.0f / 3.0f, 2.0f / 3.0f, 1.0f };
        constexpr float kPastTime      = 1.0f / 3.0f;
        /// @brief The predicted path: a point every kPathStep seconds over the 1 s horizon, each resolved against the
        ///        world (floor, sweep, landing); the samples are among them (kSampleIndex).
        /// @note Six points, not more: each costs a sweep and two or three floor queries every physics step.
        constexpr int   kPathPoints                    = 6;
        constexpr float kPathStep                      = 1.0f / kPathPoints;
        constexpr int   kSampleIndex[kFutureSamples]   = { 1, 3, 5 };
        /// @brief The path runs on past the horizon, to this many points, only while a query sample lies beyond it: motion
        ///        matching's 1 s horizon at the slowest playback rate (0.7) reaches 1.43 s. The extra legs feed nothing else.
        constexpr int   kMaxPathPoints = 9;
        /// @brief The integration substep, seconds (60 per predicted second).
        constexpr int   kSubstepsPerPoint = 10;
        constexpr float kSubstep          = kPathStep / kSubstepsPerPoint;
        constexpr float kHalfPi           = 1.5707964f;
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
        /// @brief Collide and slide: at most this many slides along hit surfaces per leg; a hit that leaves less than
        ///        kHeadOn of the remaining move along the surface (within about 15 deg of head-on) blocks; a remainder
        ///        shorter than kMinSlide (Havok units) is arrived.
        constexpr int   kMaxSlides = 3;
        constexpr float kHeadOn    = 0.25f;
        constexpr float kMinSlide  = 0.005f;
        /// @brief Collision layers the sweep passes through: other characters (layer 30) are dynamic and move away.
        constexpr std::uint64_t kIgnoreLayers = 1ull << ch::kCharControllerLayer;

        /// @brief kIgnoreLayers plus the collision filter's sensor layers, which the constraint planes zero (the body
        ///        walks through them).
        /// @note Why: 47 of 113 blocked predictions in one session hit layer 22 (ActorZone), a trigger volume.
        std::uint64_t SweepIgnoreLayers()
        {
            const hk::bhkCollisionFilter* filter = platform::hooks::Live<&hk::bhkCollisionFilter::s_singleton>();
            return kIgnoreLayers | (filter ? static_cast<std::uint64_t>(filter->m_sensorField) : 0ull);
        }

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
            float      support = 0;   ///< how much the ground there holds the character (FloorVerdict::support), 0 airborne
            bool       valid   = false;
        };

        /// @brief P1: the trajectory query.
        struct Trajectory {
            Sample                             past;
            std::array<Sample, kFutureSamples> future{};
            std::array<Sample, kFutureSamples> query{};   ///< at the caller's query times (motion matching), the gate's never
            std::array<Sample, kFutureSamples> queryPast{};   ///< the history as long ago as each query time, held at its oldest
            std::int32_t                       iState   = 0;   ///< held constant over the horizon (P1)
            float                              velForward = 0, velSide = 0;   ///< current velocity, Havok units/s
            float                              desiredForward = 0, desiredSide = 0;
            Vec2                               desiredWorld;   ///< the request in world space, Havok units/s
            float                              blockedAt = -1.0f;   ///< seconds to the first impassable hit, -1 for none
            float                              hitNz     = 0.0f;    ///< the last surface the path met: its normal z,
            int                                hitLayer  = -1;      ///< collision layer (-1 none or unknown)
            int                                hitSlides = 0;       ///< and how many slides along that leg came before it
            float                              jumpIn    = -1.0f;   ///< seconds to a pending jump's launch (its wind-up), -1 for none
        };

        /// @brief How the predicted character steers.
        /// @note With the movement model on, the frame's own step (ch::move::Step) runs on from the frame's state with its
        ///       input and caps frozen over the horizon, as GASP's trajectory does; only the caps' slope factor follows
        ///       the predicted ground. Without it the request is vanilla's, held constant in world space.
        struct Drive {
            bool            model = false;
            ch::move::State state;
            ch::move::Input input;
            ch::move::Caps  caps;
            float           slope = 0.0f;   ///< the slope factor in caps.forward; 0 = unknown, the caps stay frozen whole
            /// @brief The frame's position (game units) the predicted offsets are added to for the step.
            /// @note Anchored on the frame rather than the hull: no assumption that Havok's world origin is the game's,
            ///       at the cost of up to a frame's lag, which only bends a strafe's aim at the target.
            float           px = 0.0f, py = 0.0f;
            Vec2            desired;        ///< the request, Havok units/s: the stick at its cap under the model
        };

        /// @brief The predicted path in Havok world space, for the HUD.
        struct Path {
            std::array<ch::hud::Point, kPathPoints> at{};
            std::array<ch::Ground, kPathPoints>     ground{};
            std::array<float, kPathPoints>          support{};
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
            bool                           lastWindup  = false;
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

        /// @brief The history at `t`, or its oldest entry when it does not reach back that far; false when it is empty.
        /// @note  Why the oldest: just after a load the player stood where the history starts, as a database frame's
        ///        past holds at its clip's first frame.
        bool HistoryAtOrOldest(const State& s, double t, HistoryEntry& out)
        {
            if (HistoryAt(s, t, out)) return true;
            if (s.historyCount == 0) return false;
            const int size = static_cast<int>(s.history.size());
            out            = s.history[(s.historyHead - (s.historyCount - 1) + size) % size];
            return true;
        }

        void Reset(State& s, const void* ctrl)
        {
            s       = State{};
            s.ctrl  = ctrl;
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
        ///       falsely low support) or their reach above a downhill one (a false Airborne).
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

        /// @brief P2: one forward simulation of the Drive (the movement model's step or the held request, a critically
        ///        damped velocity spring toward it on the ground, gravity in the air), resolved against the world at every
        ///        path point: the floor under it, a sweep along the leg, and where a fall lands.
        /// @note The model steps in game units and Skyrim headings; its velocity crosses into Havok units by
        ///       kGameUnitsPerHavok (the axes are shared), its heading into the math angle as pi/2 - yaw. It steps airborne
        ///       (only the facing turns) wherever the prediction is in the air, and its velocity is the request: a hit
        ///       slides or blocks the predicted body, never the model.
        /// @note Vertical: the character starts airborne when the floor query finds no floor or it is rising faster
        ///       than the ground's slope gives (a jump). A grounded point with no floor within reach walks off a ledge:
        ///       the leg falls from its start. In the air, gravity is 1x rising and fInAirFallingCharGravityMult:HAVOK
        ///       falling (the Airborne state's), a held jump is assumed, and the horizontal velocity holds (air control
        ///       is weak and not modelled).
        /// @note Steep ground: the ground states' own slide (ch::SlideStep from the last resolved point's floor, starting
        ///       from the published slide velocity) and uphill clip (ch::ClipUphill) run here too.
        /// @param note the ground states' note (PublishedGroundNote): the slide velocity, and a pending jump that launches
        ///        at note.jumpIn seconds with note.jumpSpeed upward; empty without them.
        /// @param worldOut the future samples' positions in Havok world space, for the gate.
        /// @param queryTimes seconds ahead for Trajectory::query, ascending, each at most kMaxPathPoints * kPathStep; 0 = none.
        ///        A query sample's facing is its integration substep's; its position and height lie between the resolved
        ///        points on either side, by time; its ground is the later point's.
        Trajectory Predict(State& s, hk::bhkCharacterController& ctrl, const ch::Hull& hull, const Vec2& tangent, const Vec2& sideAxis,
                           const hk::hkVector4& velocity, const ch::GroundNote& note, const Drive& drive, std::int32_t iState,
                           const std::array<float, kFutureSamples>& queryTimes, std::array<Vec2, kFutureSamples>& worldOut, Path& path)
        {
            CB_ZONE(zone, "predictor step");
            Trajectory tr;
            tr.iState     = iState;
            tr.jumpIn     = note.jumpIn;
            tr.velForward = velocity(0) * tangent.x + velocity(1) * tangent.y;
            tr.velSide    = velocity(0) * sideAxis.x + velocity(1) * sideAxis.y;
            const Vec2 want     = drive.desired;
            tr.desiredForward   = want.x * tangent.x + want.y * tangent.y;
            tr.desiredSide      = want.x * sideAxis.x + want.y * sideAxis.y;
            tr.desiredWorld     = want;
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
                const ch::FloorVerdict pastFloor = ch::QueryFloorAt(ctrl, hull, past.x - hull.cx, past.y - hull.cy, tr.past.up);
                tr.past.ground  = pastFloor.ground;
                tr.past.support = pastFloor.support;
                tr.past.valid  = true;
            }
            for (int k = 0; k < kFutureSamples; k++) {
                HistoryEntry e;
                if (queryTimes[k] <= 0.0f || !HistoryAtOrOldest(s, s.clock - queryTimes[k], e)) continue;
                Sample& out = tr.queryPast[k];
                out.time    = -queryTimes[k];
                toLocal(e.x - hull.cx, e.y - hull.cy, out);
                out.up      = e.z - hull.feetZ;
                out.facing  = WrapAngle(e.heading - heading);
                out.valid   = true;
            }

            /// @note Without the model the facing springs toward the request's direction while there is one, else holds.
            const float wantSpeed  = std::sqrt(want.x * want.x + want.y * want.y);
            const float facingGoal = wantSpeed > kStillSpeed ? heading + WrapAngle(std::atan2(want.y, want.x) - heading) : heading;

            const hk::hkVector4& gw       = hull.world->GetGravity();
            const float          gx = gw(0) * ctrl.m_gravity, gy = gw(1) * ctrl.m_gravity, gz = gw(2) * ctrl.m_gravity;
            const float          gravity  = std::sqrt(gx * gx + gy * gy + gz * gz);
            static const float   fallMult = ch::IniFloat("fInAirFallingCharGravityMult:HAVOK", 1.35f);

            /// @note The sweep runs lifted by the step height, so what the proxy steps over (stair risers, kerbs) never
            ///       blocks; walkable faces are skipped (m_zNormal) and other characters too. Each grounded point's height
            ///       follows the floor query, so stairs and slopes keep the next leg at the floor.
            const hk::hkVector4& origin    = hull.collidable->getTransform().getTranslation();
            const float          lift      = ctrl.m_stepHeight;
            const std::uint64_t  ignore    = SweepIgnoreLayers();
            ch::FloorVerdict     prevFloor = ch::QueryFloor(ctrl, hull);
            bool airborne = prevFloor.ground == ch::Ground::Airborne || velocity(2) - ch::SlopeRise(prevFloor, velocity(0), velocity(1)) > kLaunchSpeed;

            ch::move::State model = drive.state;
            ch::move::Input input = drive.input;
            ch::move::Caps  caps  = drive.caps;
            float f = drive.model ? kHalfPi - model.yaw : heading, fv = 0.0f;
            float slopeMul = drive.slope, slopeRate = 0.0f;
            /// @note On the ground the velocity is the player's (the spring) plus the slide, as in the ground states.
            float sx = airborne ? 0.0f : note.slideX, sy = airborne ? 0.0f : note.slideY;
            float vx = velocity(0) - sx, vy = velocity(1) - sy, vz = airborne ? velocity(2) : 0.0f, ax = 0, ay = 0;
            float px = 0, py = 0, z = 0;
            Vec2  prev{};
            float prevZ = 0, prevT = 0;
            bool  blocked = false;
            bool  launched = false;
            int   sample  = 0;
            int   query   = 0;
            /// @brief Query samples whose time fell inside the current leg: their facing now, their place once it resolves.
            struct Due {
                int   index = 0;
                float frac  = 0;   ///< how far into the leg, by time
                float facing = 0;
            };
            std::array<Due, kFutureSamples> due{};
            int                             dueCount = 0;
            const float lastQuery = queryTimes.back();
            const int   points    = std::clamp(static_cast<int>(std::ceil(lastQuery / kPathStep - 1.0e-4f)), kPathPoints, kMaxPathPoints);
            for (int i = 0; i < points; i++) {
                const bool horizon = i < kPathPoints;   ///< legs past it feed only the query samples
                dueCount = 0;
                for (int k = 0; k < kSubstepsPerPoint; k++) {
                    const float h = kSubstep;
                    /// @note A jump winding up launches when its wind-up runs out, as the ground states will (ch::JumpLaunch):
                    ///       the ground velocity (slide included) carries into the air, plus the push off the ground.
                    if (!launched && !airborne && note.jumpIn >= 0.0f && static_cast<float>(i * kSubstepsPerPoint + k) * h >= note.jumpIn) {
                        launched = true;
                        airborne = true;
                        vx += sx, vy += sy;
                        sx = sy = 0.0f;
                        const float gx0 = vx, gy0 = vy;
                        ch::JumpLaunch(prevFloor, gx0, gy0, note.jumpSpeed, vx, vy, vz);
                    }
                    Vec2 d = want;
                    if (drive.model) {
                        /// @note The cap's slope factor follows the last resolved floor along the model's velocity (the
                        ///       stick from rest), smoothed as the frame smooths it.
                        if (drive.slope > 1.0e-3f && !airborne) {
                            const float vl = std::sqrt(model.vx * model.vx + model.vy * model.vy);
                            float       dx = 0.0f, dy = 0.0f;
                            if (vl > 1.0e-3f) dx = model.vx / vl, dy = model.vy / vl;
                            else if (input.haveStick) dx = input.stickX, dy = input.stickY;
                            ch::SpringDamperExact(slopeMul, slopeRate, ch::SlopeSpeedScale(prevFloor, dx, dy), ch::kSlopeSpeedHalflife, h);
                            caps.forward = drive.caps.forward * slopeMul / drive.slope;
                        }
                        input.airborne = airborne;
                        ch::move::Step(model, input, caps, drive.px + px * kGameUnitsPerHavok, drive.py + py * kGameUnitsPerHavok, h);
                        f = kHalfPi - model.yaw;
                        d = { model.vx / kGameUnitsPerHavok, model.vy / kGameUnitsPerHavok };
                    } else {
                        ch::SpringDamperExact(f, fv, facingGoal, ch::move::kFacingHalflife, h);
                    }
                    const float ox = vx + sx, oy = vy + sy, oz = vz;
                    if (!airborne) {
                        ch::ClipUphill(prevFloor, d.x, d.y);
                        ch::SpringDamperExact(vx, ax, d.x, kVelocityHalflife, h);
                        ch::SpringDamperExact(vy, ay, d.y, kVelocityHalflife, h);
                        if (!blocked) ch::SlideStep(prevFloor, gx, gy, gz, sx, sy, h);
                    } else {
                        vz -= gravity * (vz > 0.0f ? 1.0f : fallMult) * h;
                        z += (oz + vz) * 0.5f * h;
                    }
                    if (!blocked) {
                        px += (ox + vx + sx) * 0.5f * h;
                        py += (oy + vy + sy) * 0.5f * h;
                    }
                    const float t = static_cast<float>(i * kSubstepsPerPoint + k + 1) * h;
                    while (query < kFutureSamples && queryTimes[query] > 0.0f && queryTimes[query] <= t + 0.5f * h) {
                        due[dueCount++] = { query, (t - i * kPathStep) / kPathStep, f };
                        ++query;
                    }
                }

                Vec2             at{ px, py };
                ch::FloorVerdict floor;
                /// @note Held at a block, a grounded sample keeps its height and floor: settling again at the same spot
                ///       climbed the obstacle (the highest floor-like hit reaches a hull radius above the feet).
                if (!airborne && blocked) {
                    floor = prevFloor;
                    z     = prevZ;
                } else if (!airborne) {
                    floor = SettleFloor(ctrl, hull, at.x, at.y, prevZ + ch::SlopeRise(prevFloor, at.x - prev.x, at.y - prev.y), prevZ, z);
                    if (floor.ground == ch::Ground::Airborne) {
                        airborne = true;
                        vx += sx, vy += sy;
                        sx = sy = 0.0f;
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
                /// @note Collide and slide, as the proxy moves: a hit takes the leg to the surface, the rest of the leg is
                ///       projected along it and swept again (up to kMaxSlides times), and the velocities lose their part
                ///       into it. Only a head-on hit (almost nothing left along the surface) blocks. Stopping at every hit
                ///       drew an X at the root whenever a face already brushed the hull's side at the lifted start.
                if (!blocked) {
                    const Vec2  start     = at;
                    const float legLen    = std::sqrt((at.x - prev.x) * (at.x - prev.x) + (at.y - prev.y) * (at.y - prev.y));
                    Vec2        from      = prev, to = at;
                    float       fromZ     = prevZ, toZ = z;
                    float       travelled = 0.0f;
                    for (int slide = 0;; slide++) {
                        hk::hkVector4 a, b;
                        a.set(origin(0) + from.x, origin(1) + from.y, origin(2) + fromZ + lift, 0.0f);
                        b.set(origin(0) + to.x, origin(1) + to.y, origin(2) + toZ + lift, 0.0f);
                        const ch::SweepHit hit = ch::SweepFrom(hull.world, hull.collidable, a, b, kSweepStartTolerance, ctrl.m_zNormal, ignore);
                        const float dx = to.x - from.x, dy = to.y - from.y;
                        const float len = std::sqrt(dx * dx + dy * dy);
                        if (!hit.hit) {
                            at = to, z = airborne ? toZ : z;
                            break;
                        }
                        const float back    = (std::max)(0.0f, hit.fraction - (len > 1.0e-6f ? kBlockBackoff / len : 0.0f));
                        const Vec2  contact = { from.x + dx * back, from.y + dy * back };
                        const float contactZ = fromZ + (toZ - fromZ) * back;
                        travelled += len * back;
                        const hk::hkVector4& n = hit.contact.m_separatingNormal;
                        if (horizon) {
                            tr.hitNz     = n(2);
                            tr.hitLayer  = hit.body ? static_cast<int>(hit.body->getCollisionFilterInfo() & 0x7f) : -1;
                            tr.hitSlides = slide;
                        }
                        Vec2        rest   = { to.x - contact.x, to.y - contact.y };
                        const float before = std::sqrt(rest.x * rest.x + rest.y * rest.y);
                        const float nl     = std::sqrt(n(0) * n(0) + n(1) * n(1));
                        if (nl > 1.0e-4f) {
                            const Vec2 nh{ n(0) / nl, n(1) / nl };
                            auto unInto = [&nh](float& x, float& y) {
                                const float into = x * nh.x + y * nh.y;
                                if (into < 0.0f) x -= into * nh.x, y -= into * nh.y;
                            };
                            unInto(rest.x, rest.y);
                            unInto(vx, vy);
                            unInto(sx, sy);
                        }
                        const float after = std::sqrt(rest.x * rest.x + rest.y * rest.y);
                        if (before < kMinSlide) {
                            at = contact, z = airborne ? contactZ : z;
                            break;
                        }
                        if (after < kHeadOn * before || slide == kMaxSlides) {
                            blocked      = true;
                            at           = contact;
                            z            = airborne ? contactZ : z;
                            if (horizon) tr.blockedAt = prevT + kPathStep * (legLen > 1.0e-6f ? (std::min)(1.0f, travelled / legLen) : 0.0f);
                            break;
                        }
                        from = contact, fromZ = contactZ;
                        to   = { contact.x + rest.x, contact.y + rest.y };
                    }
                    if (at.x != start.x || at.y != start.y) {
                        px = at.x, py = at.y;
                        if (!airborne) floor = SettleFloor(ctrl, hull, at.x, at.y, prevZ + ch::SlopeRise(prevFloor, at.x - prev.x, at.y - prev.y), prevZ, z);
                    }
                    if (blocked) {
                        vx = vy = ax = ay = sx = sy = 0.0f;
                        if (horizon) {
                            path.blocked   = true;
                            path.blockedAt = { hull.cx + at.x, hull.cy + at.y, hull.feetZ + z };
                        }
                    }
                }
                if (floor.ground != ch::Ground::Airborne) prevFloor = floor;
                for (int d = 0; d < dueCount; d++) {
                    Sample& out = tr.query[due[d].index];
                    out.time    = queryTimes[due[d].index];
                    toLocal(prev.x + (at.x - prev.x) * due[d].frac, prev.y + (at.y - prev.y) * due[d].frac, out);
                    out.up      = prevZ + (z - prevZ) * due[d].frac;
                    out.facing  = WrapAngle(due[d].facing - heading);
                    out.ground  = floor.ground;
                    out.support = floor.support;
                    out.valid   = true;
                }
                if (!horizon) {
                    prev  = at;
                    prevZ = z;
                    prevT = (i + 1) * kPathStep;
                    continue;
                }
                path.at[i]     = { hull.cx + at.x, hull.cy + at.y, hull.feetZ + z };
                path.ground[i]  = floor.ground;
                path.support[i] = floor.support;
                if (sample < kFutureSamples && i == kSampleIndex[sample]) {
                    Sample& out = tr.future[sample];
                    out.time    = kSampleTimes[sample];
                    toLocal(at.x, at.y, out);
                    out.up           = z;
                    out.facing       = WrapAngle(f - heading);
                    out.ground       = floor.ground;
                    out.support      = floor.support;
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
                return std::format("({:.0f} fwd, {:.0f} side, {:.0f} up, {:.0f} deg, {} support {:.2f})", p.forward * kGameUnitsPerHavok,
                                   p.side * kGameUnitsPerHavok, p.up * kGameUnitsPerHavok, p.facing * kRadToDeg, ch::Name(p.ground), p.support);
            };
            LOG_INFO("predictor: trajectory ({}) t {:.2f} iState {} vel ({:.0f} fwd, {:.0f} side) desired ({:.0f} fwd, {:.0f} side) | -1/3 s {} | "
                     "1/3 s {} | 2/3 s {} | 1 s {} | blocked {}{}",
                     why, s.clock, tr.iState, tr.velForward * kGameUnitsPerHavok, tr.velSide * kGameUnitsPerHavok,
                     tr.desiredForward * kGameUnitsPerHavok, tr.desiredSide * kGameUnitsPerHavok, fmtSample(tr.past), fmtSample(tr.future[0]),
                     fmtSample(tr.future[1]), fmtSample(tr.future[2]),
                     tr.blockedAt < 0 ? std::string("no")
                                      : std::format("at {:.2f} s (hit n.z {:.2f} layer {} after {} slides)", tr.blockedAt, tr.hitNz, tr.hitLayer,
                                                    tr.hitSlides),
                     tr.jumpIn >= 0.0f ? std::format(" | jump in {:.3f} s", tr.jumpIn) : std::string());
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

            Drive drive;
            drive.desired = desired;
            ch::FrameSpeed     fs;
            const bool         haveSpeed = ch::CurrentSpeed(fs);
            ch::move::Snapshot snap;
            if (ch::move::Current(snap)) {
                drive.model = true;
                drive.state = snap.state;
                drive.input = snap.input;
                drive.caps  = snap.caps;
                drive.px    = snap.px;
                drive.py    = snap.py;
                if (haveSpeed) drive.slope = fs.slope;
                /// @note The model's request: the stick (a Skyrim heading h points along (sin h, cos h)) at the speed
                ///       the caps allow that way, game units/s into Havok units/s.
                drive.desired = {};
                if (snap.input.haveStick) {
                    const float cap = ch::move::MaxSpeed(snap.caps, snap.input.mode, std::atan2(snap.input.stickX, snap.input.stickY), snap.state.yaw) /
                                      kGameUnitsPerHavok;
                    drive.desired = { snap.input.stickX * cap, snap.input.stickY * cap };
                }
            }

            std::int32_t iState = 0;
            if (auto* player = RE::PlayerCharacter::GetSingleton()) {
                static const RE::BSFixedString kIState("iState");
                player->GetGraphVariableInt(kIState, iState);
            }

            const float speed      = std::sqrt(v(0) * v(0) + v(1) * v(1));
            const float modelSpeed = std::sqrt(drive.state.vx * drive.state.vx + drive.state.vy * drive.state.vy) / kGameUnitsPerHavok;
            const float wanted     = std::sqrt(drive.desired.x * drive.desired.x + drive.desired.y * drive.desired.y);
            const bool  active     = speed > kStillSpeed || asked > kStillSpeed || modelSpeed > kStillSpeed || wanted > kStillSpeed;

            std::array<Vec2, kFutureSamples> world{};
            Path                             path;
            ch::GroundNote note;
            ch::PublishedGroundNote(ctrl, note);
            /// @note A clip playing at `rate` reaches its horizon h after h / rate real seconds: the query is where the
            ///       prediction is then, so turns compare as well as distances.
            static_assert(kFutureSamples == CB::engine::anim::features::kHorizons.size());
            std::array<float, kFutureSamples> queryTimes{};
            if (ch::motion::On()) {
                const float rate = ch::motion::Rate(haveSpeed ? &fs : nullptr);
                for (int k = 0; k < kFutureSamples; k++) queryTimes[k] = static_cast<float>(CB::engine::anim::features::kHorizons[k]) / rate;
            }
            const Trajectory tr = Predict(s, ctrl, hull, tangent, sideAxis, v, note, drive, iState, queryTimes, world, path);
            const bool blocked  = tr.blockedAt >= 0.0f;
            /// @note The query in the clips' root space: x along the side axis (where a rightward request moves the
            ///       character), y forward, game units; a facing turned by `f` (counter-clockwise, the math angle's sense)
            ///       points along (-sin f, cos f).
            const auto allValid = [](const std::array<Sample, kFutureSamples>& a) {
                return std::all_of(a.begin(), a.end(), [](const Sample& p) { return p.valid; });
            };
            if (ch::motion::On() && allValid(tr.query) && allValid(tr.queryPast)) {
                ch::motion::Query q;
                for (int k = 0; k < kFutureSamples; k++) {
                    q.x[k]        = tr.query[k].side * kGameUnitsPerHavok;
                    q.y[k]        = tr.query[k].forward * kGameUnitsPerHavok;
                    q.dirX[k]     = -std::sin(tr.query[k].facing);
                    q.dirY[k]     = std::cos(tr.query[k].facing);
                    q.pastX[k]    = tr.queryPast[k].side * kGameUnitsPerHavok;
                    q.pastY[k]    = tr.queryPast[k].forward * kGameUnitsPerHavok;
                    q.pastDirX[k] = -std::sin(tr.queryPast[k].facing);
                    q.pastDirY[k] = std::cos(tr.queryPast[k].facing);
                }
                q.desiredX = tr.desiredWorld.x * kGameUnitsPerHavok;
                q.desiredY = tr.desiredWorld.y * kGameUnitsPerHavok;
                if (haveSpeed) q.gait = fs.gait;
                ch::motion::Search(q);
            }
            if (ch::hud::On()) {
                ch::hud::Prediction h;
                for (int k = 0; k < kFutureSamples && k < ch::hud::Prediction::kSamples; k++) {
                    h.at[k]     = { world[k].x, world[k].y, hull.feetZ + tr.future[k].up };
                    h.ground[k]  = tr.future[k].ground;
                    h.support[k] = tr.future[k].support;
                    h.time[k]   = kSampleTimes[k];
                }
                static_assert(kPathPoints <= ch::hud::Prediction::kPath);
                for (int i = 0; i < kPathPoints; i++) {
                    h.path[i]       = path.at[i];
                    h.pathGround[i]  = path.ground[i];
                    h.pathSupport[i] = path.support[i];
                }
                h.pathCount = kPathPoints;
                h.blocked   = path.blocked;
                h.blockedAt = path.blockedAt;
                h.blockedIn = tr.blockedAt;
                h.iState    = iState;
                /// @note The past path: the history every 1/12 s back to the farthest past sample, motion matching's when it
                ///       runs, else the prediction's own horizons mirrored.
                const bool  mmPast   = queryTimes.back() > 0.0f;
                const float farthest = mmPast ? queryTimes.back() : kSampleTimes[kFutureSamples - 1];
                const int   back     = (std::min)(ch::hud::Prediction::kHistory, static_cast<int>(std::ceil(farthest * 12.0f)));
                for (int i = 1; i <= back; i++) {
                    HistoryEntry e;
                    if (!HistoryAt(s, s.clock - static_cast<double>(farthest) * i / back, e)) break;
                    h.history[h.historyCount++] = { e.x, e.y, e.z };
                }
                for (int k = 0; k < kFutureSamples; k++) {
                    HistoryEntry e;
                    if (HistoryAt(s, s.clock - (mmPast ? queryTimes[k] : kSampleTimes[k]), e)) h.pastAt[h.pastCount++] = { e.x, e.y, e.z };
                }
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
            if (tr.jumpIn >= 0.0f && !s.lastWindup) why = "jump winding up";
            else if (iState != s.lastIState && s.lastIState != -1) why = "iState changed";
            else if (blocked && !s.lastBlocked) why = "blocked";
            else if (active && s.clock - s.lastDump >= kDumpPeriod) why = "periodic";
            if (why) {
                Dump(s, tr, why);
                s.lastDump = s.clock;
            }
            s.lastIState  = iState;
            s.lastBlocked = blocked;
            s.lastWindup  = tr.jumpIn >= 0.0f;
            Report(s);
        }

        [[maybe_unused]] const bool s_predictor =
            CheckSupport::Subscribe(&OnCheckSupport, platform::hooks::Priority::First, "CharPredictor", "char_predictor");

    }  // namespace

}  // namespace CB::feature
