#include "PCH.h"

#include "CharDebugHud.h"
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
        /// @warning Must match the halflife the ground-states feature (spec step 3) springs the real velocity with, or the
        ///          prediction models a different character than the one that runs. That feature has not landed yet;
        ///          change both together.
        constexpr float kVelocityHalflife = 0.08f;
        /// @brief Halflife of the facing spring toward the desired direction (P2), seconds.
        constexpr float kFacingHalflife = 0.15f;

        constexpr int   kFutureSamples = 3;
        constexpr float kSampleTimes[kFutureSamples] = { 1.0f / 3.0f, 2.0f / 3.0f, 1.0f };
        constexpr float kPastTime      = 1.0f / 3.0f;
        /// @brief The spring integration substep, seconds (60 per predicted second).
        constexpr float kSubstep = 1.0f / 60.0f;

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

        /// @brief P2: one critically damped velocity spring toward the desired velocity (held constant in world space),
        ///        a facing spring toward the desired direction, then ONE collision pass along the samples' chords.
        /// @param worldOut the future samples' positions in Havok world space, for the gate.
        Trajectory Predict(State& s, hk::bhkCharacterController& ctrl, const ch::Hull& hull, const Vec2& tangent, const Vec2& sideAxis,
                           const Vec2& velocity, const Vec2& desired, std::int32_t iState, std::array<Vec2, kFutureSamples>& worldOut)
        {
            Trajectory tr;
            tr.iState         = iState;
            tr.velForward     = velocity.x * tangent.x + velocity.y * tangent.y;
            tr.velSide        = velocity.x * sideAxis.x + velocity.y * sideAxis.y;
            tr.desiredForward = desired.x * tangent.x + desired.y * tangent.y;
            tr.desiredSide    = desired.x * sideAxis.x + desired.y * sideAxis.y;
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

            /// @note The facing goal is the desired direction while the character asks to move, else the current facing.
            const float desiredSpeed = std::sqrt(desired.x * desired.x + desired.y * desired.y);
            const float facingGoal   = desiredSpeed > kStillSpeed ? heading + WrapAngle(std::atan2(desired.y, desired.x) - heading) : heading;

            std::array<Vec2, kFutureSamples>  offset{};
            std::array<float, kFutureSamples> facing{};
            {
                float vx = velocity.x, vy = velocity.y, ax = 0, ay = 0, px = 0, py = 0;
                float f = heading, fv = 0;
                float t = 0;
                int   next = 0;
                while (next < kFutureSamples) {
                    const float h = (std::min)(kSubstep, kSampleTimes[next] - t);
                    const float ox = vx, oy = vy;
                    ch::SpringDamperExact(vx, ax, desired.x, kVelocityHalflife, h);
                    ch::SpringDamperExact(vy, ay, desired.y, kVelocityHalflife, h);
                    ch::SpringDamperExact(f, fv, facingGoal, kFacingHalflife, h);
                    px += (ox + vx) * 0.5f * h;
                    py += (oy + vy) * 0.5f * h;
                    t += h;
                    if (t >= kSampleTimes[next] - 1.0e-6f) {
                        offset[next] = { px, py };
                        facing[next] = f;
                        ++next;
                    }
                }
            }

            /// @note The sweep runs lifted by the step height, so what the proxy steps over (stair risers, kerbs) never
            ///       blocks; walkable faces are skipped (m_zNormal) and other characters too. Each sample's height follows
            ///       the floor query, so stairs and slopes keep the next leg at the floor.
            const hk::hkVector4& origin = hull.collidable->getTransform().getTranslation();
            const float          lift   = ctrl.m_stepHeight;
            Vec2                 prev{};
            float                prevZ = 0, prevT = 0;
            bool                 blocked = false;
            ch::FloorVerdict     prevFloor = ch::QueryFloor(ctrl, hull);
            for (int k = 0; k < kFutureSamples; k++) {
                Sample& out = tr.future[k];
                out.time    = kSampleTimes[k];
                Vec2 at     = blocked ? prev : offset[k];
                float z     = prevZ;
                ch::FloorVerdict floor = SettleFloor(ctrl, hull, at.x, at.y, prevZ + PlaneRise(prevFloor, at.x - prev.x, at.y - prev.y), prevZ, z);
                if (!blocked) {
                    hk::hkVector4 from, to;
                    from.set(origin(0) + prev.x, origin(1) + prev.y, origin(2) + prevZ + lift, 0.0f);
                    to.set(origin(0) + at.x, origin(1) + at.y, origin(2) + z + lift, 0.0f);
                    const ch::SweepHit hit = ch::SweepFrom(hull.world, hull.collidable, from, to, kSweepStartTolerance, ctrl.m_zNormal, kIgnoreLayers);
                    if (hit.hit) {
                        const float dx = at.x - prev.x, dy = at.y - prev.y;
                        const float len = std::sqrt(dx * dx + dy * dy);
                        const float f   = (std::max)(0.0f, hit.fraction - (len > 1.0e-6f ? kBlockBackoff / len : 0.0f));
                        blocked         = true;
                        tr.blockedAt    = prevT + (kSampleTimes[k] - prevT) * hit.fraction;
                        at              = { prev.x + dx * f, prev.y + dy * f };
                        floor = SettleFloor(ctrl, hull, at.x, at.y, prevZ + PlaneRise(prevFloor, at.x - prev.x, at.y - prev.y), prevZ, z);
                    }
                }
                if (floor.ground != ch::Ground::Airborne) prevFloor = floor;
                toLocal(at.x, at.y, out);
                out.up     = z;
                out.facing = WrapAngle(facing[k] - heading);
                out.ground = floor.ground;
                out.valid  = true;
                worldOut[k] = { hull.cx + at.x, hull.cy + at.y };
                prev        = at;
                prevZ       = z;
                prevT       = kSampleTimes[k];
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
            const Vec2 velocity{ v(0), v(1) };
            const float vmx = ctrl.m_velocityMod(0), vmy = ctrl.m_velocityMod(1);
            const Vec2  desired{ vmy * tangent.x + vmx * sideAxis.x, vmy * tangent.y + vmx * sideAxis.y };

            std::int32_t iState = 0;
            if (auto* player = RE::PlayerCharacter::GetSingleton()) {
                static const RE::BSFixedString kIState("iState");
                player->GetGraphVariableInt(kIState, iState);
            }

            const float speed   = std::sqrt(velocity.x * velocity.x + velocity.y * velocity.y);
            const float wanted  = std::sqrt(desired.x * desired.x + desired.y * desired.y);
            const bool  active  = speed > kStillSpeed || wanted > kStillSpeed;

            std::array<Vec2, kFutureSamples> world{};
            const Trajectory tr = Predict(s, ctrl, hull, tangent, sideAxis, velocity, desired, iState, world);
            const bool blocked  = tr.blockedAt >= 0.0f;
            if (ch::hud::On()) {
                ch::hud::Prediction h;
                for (int k = 0; k < kFutureSamples && k < ch::hud::Prediction::kSamples; k++) {
                    h.at[k]     = { world[k].x, world[k].y, hull.feetZ + tr.future[k].up };
                    h.ground[k] = tr.future[k].ground;
                    h.time[k]   = kSampleTimes[k];
                }
                h.blockedAt = tr.blockedAt;
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
