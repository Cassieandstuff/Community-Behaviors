#include "PCH.h"

#include "CharDebugHud.h"
#include "CharGround.h"
#include "CharMotionMatching.h"
#include "CharMovementModel.h"
#include "CharPoseSearch.h"
#include "CharSkeletonWatch.h"

#include <core/debug/Settings.h>
#include <core/resolve/LoadOrderReader.h>
#include <platform/relay/Relay.h>
#include <compile/MotionMatchingCache.h>
#include <decompile/BehaviorDecompile.h>
#include <locomotion/PoseSearch.h>
#include <core/discover/ServeKey.h>
#include <core/resolve/CacheKey.h>
#include <disk/Disk.h>
#include <platform/game/Movement.h>
#include <platform/hooks/Override.h>
#include <platform/profile/Profile.h>

#include <Behavior/Generator/Clip/hkbClipGenerator.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <format>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

/// @file
/// @brief The player's motion matching (stock-node prototype): a database of every add-on clip's per-frame features,
///        built off-thread once the load order is read; a chooser picking which of the graph's databases are searched;
///        a brute-force nearest-frame search over those; and the winner played through the add-on's graph.
/// @note  THE ADD-ON CONTRACT (Motion Matching.hky, shipped apart from CB): the behavior `kGraphKey` whose root is a
///        CBPoseSearchGenerator over clip generators (MM_Clip = the child index; a clip listed in several databases is
///        one child per database), each child a row naming its database and carrying its flags and searched range,
///        `databases` with Unreal's cost biases (condition expressions), and `chooser` rows each holding a `when`
///        condition expression; the
///        selected child bound to `kClipVar` and every child's
///        user-controlled time to `kTimeVar`; `kEnableVar`, declared in 0_master and mt_behavior, switches
///        mt_behavior's LocomotionDefault to that graph; and the chooser's inputs (kInputNames) declared on it, which CB
///        writes each frame. Without the add-on the graph is not served, nothing is built, and the controller runs
///        exactly as before.

namespace CB::feature {

    namespace {

        namespace hooks = platform::hooks;
        namespace ch    = CB::feature::controller;
        namespace relay = CB::platform::relay;
        namespace mv    = CB::feature::controller::move;
        namespace game  = CB::platform::game;
        namespace ea    = CB::engine::anim;
        namespace ps    = CB::engine::locomotion;
        using Frame     = hooks::Override<&game::MovementControllerNPC_ComputeFrameMovement>;
        using Clock     = std::chrono::steady_clock;
        using ps::Biases;
        using ps::Clip;
        using ps::Database;
        using ps::Expression;
        using ps::PoseDatabase;
        using ps::FrameOf;
        using ps::IsActive;
        using ps::kForceMoving;
        using ps::kIgnoreSurrounding;

        constexpr std::string_view kGraphKey   = "meshes/actors/character/behaviors/motionmatching.hkx";
        constexpr std::string_view kProjectDir = "meshes/actors/character/";   ///< a clip's animationName is below this
        constexpr const char*      kEnableVar  = "MM_Enable";
        constexpr const char*      kClipVar    = "MM_Clip";
        constexpr const char*      kTimeVar    = "MM_Time";

        constexpr double kSearchInterval    = 0.1;   ///< seconds between searches
        /// @brief A jump in the request since the last search searches at once, at most this soon after any search.
        constexpr double kForcedSpacing     = 0.05;
        constexpr float  kForceSpeed        = 100.0f;      ///< game units/s change of the desired speed
        constexpr float  kForceCos          = 0.8660254f;  ///< cos 30 deg: a turn of the desired direction past it

        /// @brief fMMReselectTime (settings.ini [Character], read once): seconds a pick's neighbourhood stays banned.
        /// @note  Why: 12% of switches once went A -> B -> A within a second. A forced search keeps the ban, so input
        ///        noise cannot flip.
        float ReselectTime()
        {
            static const float t = std::clamp(ch::SettingFloat("fMMReselectTime", 1.0f), 0.0f, 5.0f);
            return t;
        }
        /// @brief iMMDatabaseLOD (settings.ini [Character], read once): the MM_DatabaseLOD the chooser sees.
        int DatabaseLod()
        {
            static const int lod = static_cast<int>(std::lround(ch::SettingFloat("iMMDatabaseLOD", 0.0f)));
            return lod;
        }

        constexpr double kEndMargin         = 0.1;   ///< seconds before a clip's end that force a search

        /// @brief The playback rate's range: a clip plays at most this much slower or faster than authored; past it the
        ///        feet slide rather than the animation visibly slowing or fast-forwarding.
        constexpr float kMinRate = 0.7f, kMaxRate = 1.5f;
        /// @brief Below this asked-for speed (game units/s) the gait's rate stays as it was: standing has no speed to match.
        constexpr float kRateStill = 10.0f;

        /// @brief The chooser's inputs, GASP's (its AnimBP's chooser columns), as the MotionMatching graph declares them.
        /// @note  Values: MovementMode 0 OnGround / 1 InAir; RotationMode 0 OrientToMovement / 1 Strafe / 2 Aim (never
        ///        set: GASP's CMC character); MovementState 0 Idle / 1 Moving; Gait 0 Walk / 1 Run / 2 Sprint; Stance
        ///        0 Stand / 1 Crouch; kIsStarting..kJustTraversed are BOOL, the rest INT32.
        enum Input : int {
            kMovementMode,
            kRotationMode,
            kMovementState,
            kGait,
            kStance,
            kIsStarting,
            kIsPivoting,
            kJustLandedLight,
            kJustLandedHeavy,
            kShouldTurnInPlace,
            kShouldSpinTransition,
            kJustTraversed,
            kDatabaseLOD,
            kInputs
        };
        /// @brief The graph variables' names, byte-exact (the generator that writes the add-on declares these).
        constexpr std::array<const char*, kInputs> kInputNames{ "MM_MovementMode",      "MM_RotationMode",      "MM_MovementState",
                                                                "MM_Gait",              "MM_Stance",            "MM_IsStarting",
                                                                "MM_IsPivoting",        "MM_JustLandedLight",   "MM_JustLandedHeavy",
                                                                "MM_ShouldTurnInPlace", "MM_ShouldSpinTransition", "MM_JustTraversed",
                                                                "MM_DatabaseLOD" };
        constexpr bool IsBoolInput(int i) { return i >= kIsStarting && i <= kJustTraversed; }

        /// @brief The REAL chooser inputs. Speed2D: the horizontal speed, game units/s (GASP's is cm/s; an importer scales
        ///        its bounds by 0.7). TimeToLand: seconds until the predicted path lands (GASP's
        ///        TrajectoryCollision.TimeToLand), 0 on the ground, kNoLanding when no landing is within the horizon.
        enum RealInput : int { kSpeed2D, kTimeToLand, kRealInputs };
        constexpr std::array<const char*, kRealInputs> kRealInputNames{ "MM_Speed2D", "MM_TimeToLand" };

        /// @brief One frame's chooser inputs: the INT32 / BOOL ones by Input (`in[kGait]`), the REAL ones in `real`.
        struct Inputs {
            std::array<int, kInputs>       word{};
            std::array<float, kRealInputs> real{};
            constexpr int&       operator[](int i) { return word[i]; }
            constexpr const int& operator[](int i) const { return word[i]; }
            bool                 operator==(const Inputs&) const = default;
        };
        /// @brief Before the first frame: on the ground, orienting, idle, standing, Run (GASP's starting gait), LOD 0.
        /// @note  Settings-free: it initializes a global, before settings.ini is read.
        constexpr Inputs InitialInputs()
        {
            Inputs in{};
            in[kGait] = 1;
            return in;
        }

        /// @brief GASP's thresholds (its AnimBP's), centimetres at 0.7 game units each.
        constexpr float kMovingEpsilon   = 1.0f;          ///< game units/s: IsMoving's "velocity != 0"
        constexpr float kStartGain       = 70.0f;         ///< IsStarting: the future speed exceeds the speed by 100 cm/s
        constexpr float kPivotOrient     = 0.78539816f;   ///< IsPivoting: 45 deg orienting to movement ...
        constexpr float kPivotStrafe     = 0.52359878f;   ///< ... 30 deg strafing
        constexpr float kTurnInPlace     = 0.87266463f;   ///< ShouldTurnInPlace: 50 deg
        constexpr float kSpinAngle       = 2.2689280f;    ///< ShouldSpinTransition: 130 deg ...
        constexpr float kSpinSpeed       = 105.0f;        ///< ... at 150 cm/s or more
        constexpr float kLandHold        = 0.2f;          ///< seconds JustLanded* stays true after touching down
        /// @brief JustLandedHeavy's vertical speed (700 cm/s). @warning Assumed: GASP's HeavyLandSpeedThreshold was not
        ///        exported with the sample's assets.
        constexpr float kHeavyLandSpeed  = 490.0f;
        /// @brief GASP's future velocity spans the trajectory's 0.4 -> 0.5 s; the model is stepped at kFutureStep.
        constexpr float kFutureFrom = 0.4f, kFutureTo = 0.5f, kFutureStep = 1.0f / 60.0f;

        /// @note Published once, never freed (process lifetime): the search and the frame hook read it lock-free.
        std::atomic<const Database*> g_db{ nullptr };

        /// @brief What plays: a clip and the time in it.
        struct Playback {
            int    clip = -1;
            double time = 0;
        };
        struct Shared {
            std::mutex        lock;
            Playback          now;
            Playback          pending;
            bool              havePending = false;
            bool              searchNow   = true;
            /// @brief GASP's InterruptOnDatabaseChange fired: the next search runs at once and the playing row cannot go on.
            bool              interrupt   = false;
            /// @brief Per database: searched (the chooser's latest pick; empty until the first frame = every database).
            std::vector<char> active;
            /// @brief Per database: its biases as of the latest frame (empty until the first frame = every bias 0).
            std::vector<Biases> biases;
            std::vector<char> interruptActive;   ///< the pick the interrupt saw, which its search uses
            Inputs            inputs = InitialInputs();
        };
        Shared             g_shared;
        /// @brief The playback rate (clip seconds per second): written by the predictor's Rate, read by the frame hook.
        std::atomic<float> g_rate{ 1.0f };
        /// @brief The predictor's landing time (SetTimeToLand), read by the frame hook's MM_TimeToLand while airborne.
        std::atomic<float> g_timeToLand{ controller::motion::kNoLanding };
        /// @brief When the last switch was chosen (steady_clock ticks; 0 = none yet), for Describe.
        std::atomic<Clock::rep> g_lastSwitch{ 0 };
        std::atomic<int>   g_logs{ 0 };
        bool LogBudget() { return g_logs.fetch_add(1, std::memory_order_relaxed) < 400; }

        /// @brief The searches' biased costs since the last report: the best candidate's and the playing row's.
        struct CostLog {
            std::mutex         lock;
            std::vector<float> winning, playing;
        };
        CostLog g_costs;
        constexpr std::size_t kCostLogCap = 4096;

        std::filesystem::path CachePath()
        {
            return std::filesystem::current_path() / "Data" / core::discover::kConsolidatedCacheDir / core::discover::kMotionMatchingCacheFile;
        }

        /// @brief The cache's clips by animationName (byte-exact); empty when there is none or it cannot be read.
        std::map<std::string, ea::MmCachedClip> LoadCache()
        {
            std::map<std::string, ea::MmCachedClip> out;
            std::error_code ec;
            if (!std::filesystem::exists(CachePath(), ec)) return out;
            std::string err;
            const auto  bytes = CB::engine::disk::ReadBytes(CachePath());
            if (!bytes) err = "unreadable";
            std::vector<ea::MmCachedClip> clips;
            if (!bytes || !ea::ReadMmCache(*bytes, clips, err)) {
                LOG_INFO("motion matching: cache not used ({}) - measuring every clip", err);
                return out;
            }
            for (auto& c : clips) {
                std::string name = c.name;
                out.emplace(std::move(name), std::move(c));
            }
            return out;
        }

        /// @brief The clip `name` (an animationName), from `cache` when its unit is unchanged, else measured now.
        /// @warning Throws when it cannot be measured.
        /// @note  The fingerprint folds in `compiler` (the plugin DLL + schema): a rebuild may measure differently, so it
        ///        measures every clip once.
        ea::MmCachedClip MeasureOrCached(const core::resolve::LoadOrderReader& reader, const std::string& name, std::string_view compiler,
                                         const std::map<std::string, ea::MmCachedClip>& cache, bool& hit)
        {
            std::string path = std::string(kProjectDir) + name;
            std::replace(path.begin(), path.end(), '\\', '/');
            const auto text = reader.nativeAnimation(path);
            if (!text) throw std::runtime_error("not shipped by any bundle");
            const std::uint64_t fp = ea::MmFingerprint(std::string(compiler) + '\n' + *text);
            if (const auto it = cache.find(name); it != cache.end() && it->second.fingerprint == fp) {
                hit = true;
                return it->second;
            }
            hit = false;
            return ea::MeasureMmClip(name, path, *text, fp);
        }

        /// @brief Measure every clip the add-on graph plays and publish the database.
        /// @note Runs on its own below-normal thread: the add-on's authored YAML takes seconds to parse. Each distinct
        ///       animationName is measured once (a clip may be a child of several databases) and its rows are appended
        ///       once per child that plays it.
        void Build(const core::resolve::LoadOrderReader& reader)
        {
            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
            const auto t0 = Clock::now();
            CB::engine::decompile::ParsedHky graph;
            if (!reader.mergedGraph(std::string(kGraphKey), graph)) {
                LOG_INFO("motion matching: '{}' is not in the load order (no Motion Matching.hky database bundle installed) - off", kGraphKey);
                return;
            }
            auto* selector = ps::RootSelector(graph);
            if (!selector) {
                LOG_WARN("motion matching: '{}' does not have a CBPoseSearchGenerator at its root - off", kGraphKey);
                return;
            }

            const auto                    cache    = LoadCache();
            const std::string             compiler = core::resolve::CompilerIdentity(std::filesystem::current_path() / "Data");
            std::vector<ea::MmCachedClip> fresh;
            std::size_t                   cacheHits  = 0;
            bool                          cacheDirty = false;
            const auto clipOf = [&](const std::string& name) -> const ea::MmCachedClip& {
                bool hit = false;
                fresh.push_back(MeasureOrCached(reader, name, compiler, cache, hit));
                if (hit) ++cacheHits;
                else cacheDirty = true;
                return fresh.back();
            };
            ps::BuildReport report;
            auto db = ps::BuildDatabase(*selector, clipOf, [](const std::string& message) { LOG_WARN("motion matching: {}", message); }, &report);

            /// @note Rewritten when a clip was measured now or a cached one is no longer played, so the file holds
            ///       exactly this load order's clips.
            if (cacheDirty || fresh.size() != cache.size()) {
                std::vector<std::uint8_t> bytes;
                std::string               err;
                std::error_code           ec;
                std::filesystem::create_directories(CachePath().parent_path(), ec);
                if (!ea::WriteMmCache(fresh, bytes, err) || !CB::engine::disk::WriteFile(CachePath(), bytes, {}, &err))
                    LOG_WARN("motion matching: cache not written ({})", err);
            }
            LOG_INFO("motion matching: {} of {} clip(s) from the cache", cacheHits, fresh.size());
            if (!db) {
                LOG_WARN("motion matching: no clip of '{}' could be measured - off", kGraphKey);
                return;
            }

            const double secs = std::chrono::duration<double>(Clock::now() - t0).count();
            LOG_INFO("motion matching: database built in {:.1f} s - {} child(ren) over {} distinct clip(s) ({} measured), {} database(s), "
                     "chooser {}; {} frames x {} features; clip speeds walk {:.0f} ({} frames), run {:.0f} ({}), sprint {:.0f} ({}) "
                     "game units/s",
                     secs, db->clips.size(), report.distinctClips, report.measuredClips, db->databases.size(),
                     db->haveChooser ? std::format("{} row(s), {} fallback", db->chooser.size(), db->fallback.size()) : std::string("none"),
                     report.frames, CB::engine::anim::features::kDims, db->gaitSpeed[1], report.gaitFrames[1], db->gaitSpeed[2], report.gaitFrames[2],
                     db->gaitSpeed[3], report.gaitFrames[3]);
            const auto text = [](const Expression& e) { return e.text.empty() ? std::string("0") : "`" + e.text + "`"; };
            for (const PoseDatabase& d : db->databases)
                LOG_INFO("motion matching: database '{}' - bias base {}, continuing {}, looping {}{}", d.name, text(d.base),
                         text(d.continuing), text(d.looping), d.pivots ? ", Pivots" : "");
            for (std::size_t r = 0; r < db->chooser.size(); ++r)
                LOG_INFO("motion matching: chooser row {} - when {} -> {}", r, db->chooser[r].when.text.empty() ? std::string("always") : text(db->chooser[r].when),
                         db->chooser[r].databases.size());
            g_db.store(db.release(), std::memory_order_release);
        }

        [[maybe_unused]] const bool s_build = [] {
            if (!ch::motion::On()) return false;
            core::resolve::OnLoadOrderReady([](const core::resolve::LoadOrderReader& reader) {
                std::thread([reader] { Build(reader); }).detach();
            });
            return true;
        }();

        RE::Actor* PlayerOf(RE::MovementControllerNPC* controller)
        {
            auto* player = RE::PlayerCharacter::GetSingleton();
            if (!player) return nullptr;
            const auto& mc = player->GetActorRuntimeData().movementController;
            return mc && static_cast<void*>(mc.get()) == controller ? player : nullptr;
        }

        /// @brief GASP's future velocity's speed: the displacement over kFutureFrom -> kFutureTo ahead, game units/s.
        /// @note  An approximation of GASP's trajectory sample: the movement model stepped on from the frame's snapshot
        ///        with its input frozen (the predictor's own step), integrated directly, without the predictor's velocity
        ///        spring, collisions, floors or falls.
        float FutureSpeed(const mv::Snapshot& snap)
        {
            mv::State   s    = snap.state;
            float       px   = snap.px, py = snap.py, ax = px, ay = py;
            const int   from = static_cast<int>(std::lround(kFutureFrom / kFutureStep));
            const int   to   = static_cast<int>(std::lround(kFutureTo / kFutureStep));
            for (int i = 0; i < to; ++i) {
                if (i == from) ax = px, ay = py;
                mv::Step(s, snap.input, snap.caps, snap.profile, px, py, kFutureStep);
                px += s.vx * kFutureStep, py += s.vy * kFutureStep;
            }
            return std::hypot(px - ax, py - ay) / (kFutureTo - kFutureFrom);
        }

        /// @brief Frame-to-frame memory of the chooser inputs. @note The frame hook's alone: no lock.
        struct InputMemory {
            bool  haveZ = false;
            float lastZ = 0;
            float airVz = 0;          ///< the vertical speed on the latest airborne frame, game units/s
            bool  wasAirborne = false;
            float landedFor = -1;     ///< seconds since touching down, -1 = not landed since the last air time
            bool  heavy = false;
            bool  haveStickYaw = false;
            float stickYaw = 0;       ///< the stick's latest heading (held after its release)
            float turnHold = 0;       ///< seconds ShouldTurnInPlace stays raised
        };

        /// @brief GASP's chooser inputs for this frame into `io` (its previous values on entry).
        /// @param playingPivots the playing child's database is tagged "Pivots".
        /// @return false without a fresh movement-model frame: `io` keeps its values.
        bool ComputeInputs(RE::Actor* player, float dt, bool playingPivots, Inputs& io)
        {
            static InputMemory m;
            const float z  = player->GetPosition().z;
            const float vz = m.haveZ && dt > 0.0f ? (z - m.lastZ) / dt : 0.0f;
            m.lastZ = z, m.haveZ = true;

            mv::Snapshot snap;
            if (!mv::Current(snap)) return false;
            const mv::State& s      = snap.state;
            const mv::Input& in     = snap.input;
            const float      speed  = std::hypot(s.vx, s.vy);
            const bool       moving = speed > kMovingEpsilon && in.haveStick;
            const bool       strafe = in.mode == mv::Mode::Strafe;
            const bool       wasMoving = io[kMovementState] == 1;
            const float      velYaw = std::atan2(s.vx, s.vy);
            if (in.haveStick) m.stickYaw = std::atan2(in.stickX, in.stickY), m.haveStickYaw = true;

            io[kMovementMode]  = in.airborne ? 1 : 0;
            io[kRotationMode]  = strafe ? 1 : 0;
            io[kMovementState] = moving ? 1 : 0;
            if (in.gait != ch::Gait::None) io[kGait] = static_cast<int>(in.gait) - 1;
            io[kStance]        = player->IsSneaking() ? 1 : 0;

            io[kIsStarting] = moving && !playingPivots && FutureSpeed(snap) >= speed + kStartGain;
            io[kIsPivoting] = moving && std::fabs(ch::WrapPi(m.stickYaw - velYaw)) >= (strafe ? kPivotStrafe : kPivotOrient);

            /// @note The landing's vertical speed is the player's height change over the last airborne frame (the
            ///       touch-down frame's own is already cut short by the floor).
            if (in.airborne) m.airVz = vz, m.landedFor = -1.0f;
            else if (m.wasAirborne) m.landedFor = 0.0f, m.heavy = std::fabs(m.airVz) >= kHeavyLandSpeed;
            else if (m.landedFor >= 0.0f) m.landedFor += dt;
            m.wasAirborne     = in.airborne;
            const bool landed = m.landedFor >= 0.0f && m.landedFor < kLandHold;
            io[kJustLandedLight] = landed && !m.heavy;
            io[kJustLandedHeavy] = landed && m.heavy;

            /// @note The orientation intent: strafing, toward the lock target; orienting, the stick's latest heading,
            ///       since the stop that raises it releases the stick (GASP's capsule likewise keeps the last input's
            ///       rotation while its visual root lags behind).
            /// @note Held kSearchInterval: GASP searches every tick, CB's search runs at most that often, and a one-frame
            ///       input would mostly miss it.
            float intent = s.yaw;
            if (strafe) {
                const float dx = in.targetX - snap.px, dy = in.targetY - snap.py;
                if (in.haveTarget && dx * dx + dy * dy > 1.0f) intent = std::atan2(dx, dy);
            } else if (m.haveStickYaw) {
                intent = m.stickYaw;
            }
            if (wasMoving && !moving && std::fabs(ch::WrapPi(intent - s.yaw)) >= kTurnInPlace) m.turnHold = static_cast<float>(kSearchInterval);
            else m.turnHold = (std::max)(0.0f, m.turnHold - dt);
            io[kShouldTurnInPlace] = m.turnHold > 0.0f;

            io[kShouldSpinTransition] = !playingPivots && speed >= kSpinSpeed && std::fabs(ch::WrapPi(velYaw - s.yaw)) >= kSpinAngle;
            io[kJustTraversed]        = 0;
            io[kDatabaseLOD]          = DatabaseLod();
            io.real[kSpeed2D]         = speed;
            io.real[kTimeToLand]      = in.airborne ? g_timeToLand.load(std::memory_order_relaxed) : 0.0f;
            return true;
        }

        void WriteInputs(RE::Actor* player, const Inputs& in)
        {
            static const std::array<RE::BSFixedString, kInputs> names = [] {
                std::array<RE::BSFixedString, kInputs> a;
                for (int i = 0; i < kInputs; ++i) a[i] = kInputNames[i];
                return a;
            }();
            for (int i = 0; i < kInputs; ++i) {
                if (IsBoolInput(i)) player->SetGraphVariableBool(names[i], in[i] != 0);
                else player->SetGraphVariableInt(names[i], in[i]);
            }
            static const std::array<RE::BSFixedString, kRealInputs> realNames = [] {
                std::array<RE::BSFixedString, kRealInputs> a;
                for (int i = 0; i < kRealInputs; ++i) a[i] = kRealInputNames[i];
                return a;
            }();
            for (int i = 0; i < kRealInputs; ++i) player->SetGraphVariableFloat(realNames[i], in.real[i]);
        }

        /// @brief GASP's Get_MMInterruptMode: the movement mode changed, or on the ground the movement state, the gait
        ///        while moving, or the stance did.
        bool InputsInterrupt(const Inputs& before, const Inputs& now)
        {
            if (before[kMovementMode] != now[kMovementMode]) return true;
            if (now[kMovementMode] != 0) return false;
            return before[kMovementState] != now[kMovementState] || (now[kMovementState] == 1 && before[kGait] != now[kGait]) ||
                   before[kStance] != now[kStance];
        }

        /// @brief The chooser inputs, by their exact names, as GraphVar reads them: this frame's values, typed as declared.
        /// @note  Why ahead of the graph: the inputs are declared on the add-on's graph, not the root one GraphVar takes
        ///        types from, and the frame knows them before the graph is evaluated.
        class InputVariables final : public relay::VariableSource {
        public:
            explicit InputVariables(const Inputs& in) : m_in(in) {}
            bool Read(const char* name, relay::Value& out) const override
            {
                for (int i = 0; i < kInputs; ++i)
                    if (std::strcmp(name, kInputNames[i]) == 0) {
                        out = IsBoolInput(i) ? relay::Value::Bool(m_in[i] != 0) : relay::Value::Int(m_in[i]);
                        return true;
                    }
                for (int i = 0; i < kRealInputs; ++i)
                    if (std::strcmp(name, kRealInputNames[i]) == 0) {
                        out = relay::Value::Float(m_in.real[i]);
                        return true;
                    }
                return false;
            }

        private:
            const Inputs& m_in;
        };

        /// @brief The frame's evaluation pass over the player: the chooser and every bias share its primitive reads.
        /// @note  The frame hook's alone. `vars` must outlive the frame's evaluations.
        const relay::EvalContext& FrameContext(RE::Actor* player, const InputVariables& vars)
        {
            static relay::EvalContext ctx;
            ctx.Reset(player);
            ctx.variables = &vars;
            return ctx;
        }

        std::string ActiveNames(const Database& db, const std::vector<char>& active)
        {
            std::string out;
            for (std::size_t d = 0; d < db.databases.size(); ++d)
                if (IsActive(active, static_cast<int>(d))) out += (out.empty() ? "" : ", ") + db.databases[d].name;
            return out.empty() ? std::string("none") : out;
        }

        std::string InputsText(const Inputs& in)
        {
            static constexpr const char* kGaits[3] = { "walk", "run", "sprint" };
            std::string out = std::format("{} {} {} {} {}", in[kMovementMode] ? "air" : "ground", in[kRotationMode] ? "strafe" : "orient",
                                          in[kMovementState] ? "moving" : "idle", kGaits[std::clamp(in[kGait], 0, 2)],
                                          in[kStance] ? "crouch" : "stand");
            static constexpr std::pair<int, const char*> kFlags[] = {
                { kIsStarting, "starting" },    { kIsPivoting, "pivoting" },          { kJustLandedLight, "landed" },
                { kJustLandedHeavy, "landed-heavy" }, { kShouldTurnInPlace, "turn-in-place" }, { kShouldSpinTransition, "spin" },
                { kJustTraversed, "traversed" }
            };
            for (const auto& [i, name] : kFlags)
                if (in[i]) out += std::string(" ") + name;
            if (in[kDatabaseLOD]) out += std::format(" lod{}", in[kDatabaseLOD]);
            out += std::format(" {:.0f}/s", in.real[kSpeed2D]);
            if (in[kMovementMode]) out += in.real[kTimeToLand] >= controller::motion::kNoLanding
                                              ? std::string(" no-landing")
                                              : std::format(" lands-in {:.2f}s", in.real[kTimeToLand]);
            return out;
        }

        void CountFrame();

        /// @brief Plays what the search chose: advances it by the frame's time and hands it to the add-on graph; computes
        ///        the chooser's inputs, writes them to the graph and publishes the chooser's pick and the databases' biases
        ///        for the search.
        void OnFrame(Frame::Next next, RE::MovementControllerNPC* controller, float deltaTime, game::MovementFrame* out)
        {
            next(controller, deltaTime, out);
            RE::Actor*       player = out ? PlayerOf(controller) : nullptr;
            const Database*  db     = g_db.load(std::memory_order_acquire);
            if (!player) return;
            ch::motion::QueueSkeletonWatch();
            if (!db) return;
            CountFrame();
            CB_PLOT("MM rate", g_rate.load(std::memory_order_relaxed));

            Playback p;
            {
                std::scoped_lock lock(g_shared.lock);
                Playback& now = g_shared.now;
                if (g_shared.havePending) {
                    now                  = g_shared.pending;
                    g_shared.havePending = false;
                } else if (now.clip >= 0) {
                    now.time += deltaTime * g_rate.load(std::memory_order_relaxed);
                }
                if (now.clip >= 0) {
                    const Clip& c = db->clips[now.clip];
                    if (now.time >= c.duration - kEndMargin) g_shared.searchNow = true;
                    now.time = (std::min)(now.time, c.duration);
                }
                p = now;
            }

            /// @note The frame hook's alone: the inputs as of the previous frame, then this one's.
            static Inputs     inputs = InitialInputs();
            static std::vector<char> lastActive;
            const int         playingDb = p.clip >= 0 ? db->clips[p.clip].database : -1;
            const Inputs      before    = inputs;
            ComputeInputs(player, deltaTime, playingDb >= 0 && db->databases[playingDb].pivots, inputs);
            WriteInputs(player, inputs);
            const InputVariables     vars(inputs);
            const relay::EvalContext& ctx       = FrameContext(player, vars);
            const std::vector<char>  active    = ps::Choose(*db, ctx);
            std::vector<Biases>      biases    = ps::EvaluateBiases(*db, ctx);
            const bool               interrupt = playingDb >= 0 && !IsActive(active, playingDb) && InputsInterrupt(before, inputs);
            {
                std::scoped_lock lock(g_shared.lock);
                g_shared.active = active;
                g_shared.biases = std::move(biases);
                g_shared.inputs = inputs;
                if (interrupt) g_shared.interrupt = true, g_shared.interruptActive = active;
            }
            if (active != lastActive) {
                static int changes = 0;
                if (changes++ < 300)
                    LOG_INFO("motion matching: searching {} ({}){}", ActiveNames(*db, active), InputsText(inputs),
                             interrupt ? std::format(" - interrupts '{}'", db->databases[playingDb].name) : std::string());
                lastActive = active;
            }

            /// @note Until the first search chooses a clip, MM_Enable stays at its declared 0: vanilla locomotion plays.
            if (p.clip < 0) return;
            static const RE::BSFixedString enableName(kEnableVar), clipName(kClipVar), timeName(kTimeVar);
            const Clip& c = db->clips[p.clip];
            player->SetGraphVariableInt(enableName, 1);
            player->SetGraphVariableInt(clipName, p.clip);
            player->SetGraphVariableFloat(timeName, static_cast<float>(c.duration > 0 ? p.time / c.duration : 0.0));
        }

        [[maybe_unused]] const bool s_subscribed =
            Frame::Subscribe(&OnFrame, hooks::Priority::Normal, "CharMotionMatching", "char_motion_matching");

        /// @brief How often clips are sampled: the proof that CBPoseSearchGenerator's skipped children are not.
        /// @note  Kept every child active, so a skip the engine ignored would sample all 252 add-on clips every frame.
        using ClipGenerate = hooks::Override<&CB::havok::hkbClipGenerator::generate>;
        constexpr std::string_view kClipPrefix = "animations\\MotionMatching\\";   ///< the add-on's clips, as authored
        constexpr double           kReportEvery = 5.0;   ///< seconds
        struct GenerateCount {
            std::atomic<std::uint64_t> ours{ 0 }, all{ 0 }, frames{ 0 };
            std::atomic<std::uint64_t> oursThisFrame{ 0 };   ///< add-on clip samples since the last player frame (the plot)
        };
        GenerateCount g_generates;

        void OnClipGenerate(ClipGenerate::Next next, CB::havok::hkbClipGenerator* clip, const CB::havok::hkbContext& context,
                            const CB::havok::hkbGeneratorOutput** activeChildrenOutput, CB::havok::hkbGeneratorOutput& output,
                            CB::havok::hkReal timeOffset)
        {
            g_generates.all.fetch_add(1, std::memory_order_relaxed);
            if (const char* name = clip->m_animationName.cString(); name && std::string_view(name).starts_with(kClipPrefix)) {
                g_generates.ours.fetch_add(1, std::memory_order_relaxed);
                g_generates.oursThisFrame.fetch_add(1, std::memory_order_relaxed);
            }
            next(clip, context, activeChildrenOutput, output, timeOffset);
        }

        [[maybe_unused]] const bool s_countSubscribed =
            ClipGenerate::Subscribe(&OnClipGenerate, hooks::Priority::Normal, "CharMotionMatching", "char_motion_matching");

        /// @brief "min / median / max (count)" of `v`, which it reorders.
        std::string Range(std::vector<float>& v)
        {
            if (v.empty()) return "none";
            std::sort(v.begin(), v.end());
            return std::format("{:.2f} / {:.2f} / {:.2f} ({})", v.front(), v[v.size() / 2], v.back(), v.size());
        }

        /// @brief Count a player frame; every kReportEvery seconds, log the clip samples per frame and the searches'
        ///        biased cost ranges since the last report (the scale the databases' biases act on).
        void CountFrame()
        {
            g_generates.frames.fetch_add(1, std::memory_order_relaxed);
            CB_PLOT("MM add-on clip samples", g_generates.oursThisFrame.exchange(0, std::memory_order_relaxed));
            static Clock::time_point last = Clock::now();
            const auto               now  = Clock::now();
            if (std::chrono::duration<double>(now - last).count() < kReportEvery) return;
            last               = now;
            const auto frames  = g_generates.frames.exchange(0);
            const auto ours    = g_generates.ours.exchange(0);
            const auto all     = g_generates.all.exchange(0);
            std::vector<float> winning, playing;
            {
                std::scoped_lock lock(g_costs.lock);
                winning.swap(g_costs.winning);
                playing.swap(g_costs.playing);
            }
            static int reports = 0;
            if (reports == 0)
                LOG_INFO("motion matching: switching - additive database cost biases, reselect ban {:.2f} s, forced on {:.0f} "
                         "units/s or 30 deg, database LOD {}",
                         ReselectTime(), kForceSpeed, DatabaseLod());
            if (frames > 0 && reports++ < 60)
                LOG_INFO("motion matching: clip samples per frame - add-on clips {:.2f}, every clip {:.1f} (over {} frames); "
                         "biased cost min / median / max - best candidate {}, playing {}",
                         static_cast<double>(ours) / frames, static_cast<double>(all) / frames, frames, Range(winning), Range(playing));
        }

    }  // namespace

    bool controller::motion::On()
    {
        static const bool on = core::debug::FlagOn("char_motion_matching");
        return on;
    }

    void controller::motion::SetTimeToLand(float seconds) { g_timeToLand.store(seconds, std::memory_order_relaxed); }

    std::string controller::motion::Describe()
    {
        const Database* db = g_db.load(std::memory_order_acquire);
        if (!db) return "no database";
        Playback p;
        {
            std::scoped_lock lock(g_shared.lock);
            p = g_shared.now;
        }
        if (p.clip < 0) return "no clip chosen yet";
        const Clip&      c    = db->clips[p.clip];
        const Clock::rep at   = g_lastSwitch.load(std::memory_order_relaxed);
        const double     ago  = at ? std::chrono::duration<double>(Clock::now() - Clock::time_point(Clock::duration(at))).count() : -1.0;
        return std::format("clip {} '{}' ({}) frame {}, blend left {:.2f} s, last switch {:.2f} s ago", p.clip, c.name,
                           c.database >= 0 ? db->databases[c.database].name : std::string("no database"),
                           c.frames > 0 ? FrameOf(c, p.time) : 0, BlendRemaining(), ago);
    }

    float controller::motion::Rate(const FrameSpeed* frame)
    {
        const Database* db   = g_db.load(std::memory_order_acquire);
        const float     last = g_rate.load(std::memory_order_relaxed);
        if (!db || !frame || frame->gait == Gait::None || frame->full < kRateStill) return last;
        const float clipSpeed = db->gaitSpeed[static_cast<std::size_t>(frame->gait)];
        if (clipSpeed <= 0.0f) return last;
        const float rate = std::clamp(frame->full / clipSpeed, kMinRate, kMaxRate);
        g_rate.store(rate, std::memory_order_relaxed);
        return rate;
    }

    void controller::motion::Search(const Query& q)
    {
        const Database* db = g_db.load(std::memory_order_acquire);
        if (!db) return;
        CB_ZONE(zone, "MM search");
        /// @note Only the predictor thread searches, so the search's own history needs no lock.
        static Clock::time_point last{};
        static float             lastWantX = 0, lastWantY = 0;
        const auto               now     = Clock::now();
        const double             elapsed = std::chrono::duration<double>(now - last).count();

        /// @note Forced: the request jumped since the last search, in speed or (while moving) in direction.
        const float speed = std::hypot(q.desiredX, q.desiredY), lastSpeed = std::hypot(lastWantX, lastWantY);
        bool        forced = std::fabs(speed - lastSpeed) > kForceSpeed;
        if (!forced && speed > kForceMoving && lastSpeed > kForceMoving)
            forced = (q.desiredX * lastWantX + q.desiredY * lastWantY) / (speed * lastSpeed) < kForceCos;

        Playback          cur;
        bool              clipEnd = false, interrupt = false;
        std::vector<char>   active;
        std::vector<Biases> biases;
        Inputs              inputs = InitialInputs();
        {
            std::scoped_lock lock(g_shared.lock);
            clipEnd   = g_shared.searchNow;
            interrupt = g_shared.interrupt;
            if (!clipEnd && !interrupt && elapsed < kSearchInterval && !(forced && elapsed >= kForcedSpacing)) return;
            g_shared.searchNow = false;
            g_shared.interrupt = false;
            active             = interrupt ? g_shared.interruptActive : g_shared.active;
            biases             = g_shared.biases;
            inputs             = g_shared.inputs;
            cur                = g_shared.havePending ? g_shared.pending : g_shared.now;
        }
        last = now, lastWantX = q.desiredX, lastWantY = q.desiredY;
        const char* trigger = interrupt ? "interrupt" : clipEnd ? "clip end" : elapsed < kSearchInterval ? "forced" : "timer";

        /// @note The reselect ban: each pick of the last fMMReselectTime, advanced by the clip time played since (at the
        ///       current rate), bans the rows within kIgnoreSurrounding of where it would be now, as merged row ranges.
        struct Pick {
            int               clip  = 0;
            double            frame = 0;
            Clock::time_point at;
        };
        static std::vector<Pick> picks;
        const float              reselect = ReselectTime();
        const float              rate     = g_rate.load(std::memory_order_relaxed);
        std::erase_if(picks, [&](const Pick& p) { return std::chrono::duration<double>(now - p.at).count() > reselect; });
        std::vector<std::pair<int, int>> banned;
        for (const Pick& p : picks) {
            const Clip&  c  = db->clips[p.clip];
            const double at = p.frame + std::chrono::duration<double>(now - p.at).count() * rate / c.dt;
            const int    lo = (std::max)(0, static_cast<int>(std::floor(at)) - kIgnoreSurrounding);
            const int    hi = (std::min)(c.frames - 1, static_cast<int>(std::ceil(at)) + kIgnoreSurrounding);
            if (lo <= hi) banned.emplace_back(c.first + lo, c.first + hi);
        }
        std::sort(banned.begin(), banned.end());

        ps::Query query;
        query.x = q.x, query.y = q.y, query.dirX = q.dirX, query.dirY = q.dirY;
        query.pastX = q.pastX, query.pastY = q.pastY, query.pastDirX = q.pastDirX, query.pastDirY = q.pastDirY;
        query.desiredX    = q.desiredX;
        query.desiredY    = q.desiredY;
        query.currentClip = cur.clip;
        query.currentTime = cur.time;
        query.active      = std::move(active);
        query.biases      = std::move(biases);
        query.banned      = std::move(banned);
        query.clipEnd     = clipEnd;
        query.interrupt   = interrupt;

        const auto             t0       = Clock::now();
        const ps::SearchResult result   = ps::Search(*db, query);
        const float            searchMs = std::chrono::duration<float, std::milli>(Clock::now() - t0).count();
        float                  best     = result.best;
        int                    bestRow  = result.bestRow;
        const int              curRow   = result.playingRow;
        const float            playing  = result.playing;
        const std::size_t      searched = result.searched;
        CB_PLOT("MM search ms", searchMs);
        {
            std::scoped_lock lock(g_costs.lock);
            if (bestRow >= 0 && g_costs.winning.size() < kCostLogCap) g_costs.winning.push_back(best);
            if (curRow >= 0 && g_costs.playing.size() < kCostLogCap) g_costs.playing.push_back(playing);
        }

        static int switches = 0;
        if (result.wins) {
            const int   clip = db->clipOf[bestRow];
            const Clip& c    = db->clips[clip];
            const char* dbName = c.database >= 0 ? db->databases[c.database].name.c_str() : "-";
            {
                std::scoped_lock lock(g_shared.lock);
                g_shared.pending     = { clip, (bestRow - c.first) * c.dt };
                g_shared.havePending = true;
            }
            picks.push_back({ clip, static_cast<double>(bestRow - c.first), now });
            ++switches;
            g_lastSwitch.store(now.time_since_epoch().count(), std::memory_order_relaxed);
            CB_PLOT("MM switches", switches);
            CB_MESSAGE("MM -> clip {} '{}' [{}] frame {} ({}, cost {:.2f}, was {:.2f})", clip, c.name, dbName, bestRow - c.first, trigger,
                       best, playing);
            static constexpr const char* kGaitNames[4] = { "none", "walk", "run", "sprint" };
            if (LogBudget())
                LOG_INFO("motion matching: -> clip {} '{}' in '{}' frame {} x{:.2f} (cost {:.2f}, was {:.2f}; {}; gait {} / frame {}; "
                         "{} of {} rows in {:.2f} ms)",
                         clip, c.name, dbName, bestRow - c.first, rate, best, playing, trigger,
                         kGaitNames[static_cast<int>(q.gait) & 3], kGaitNames[db->rowGait[bestRow] & 3], searched, db->clipOf.size(),
                         searchMs);
        } else {
            best    = playing;
            bestRow = curRow;
        }

        if (ch::hud::On()) {
            ch::hud::Matching h;
            const int   shown = bestRow >= 0 ? db->clipOf[bestRow] : -1;
            if (shown >= 0) {
                const Clip&        c     = db->clips[shown];
                const auto         slash = c.name.find_last_of('\\');
                const std::string  stem  = slash == std::string::npos ? c.name : c.name.substr(slash + 1);
                std::snprintf(h.clip, sizeof(h.clip), "%s", stem.c_str());
                std::snprintf(h.database, sizeof(h.database), "%s", c.database >= 0 ? db->databases[c.database].name.c_str() : "-");
                h.frame = bestRow - c.first;
            }
            std::snprintf(h.active, sizeof(h.active), "%s", ActiveNames(*db, query.active).c_str());
            std::snprintf(h.chooser, sizeof(h.chooser), "%s", InputsText(inputs).c_str());
            h.cost     = best;
            h.searchMs = searchMs;
            h.switches = switches;
            h.rate     = g_rate.load(std::memory_order_relaxed);
            h.blend    = BlendRemaining();
            ch::hud::Publish(h);
        }
    }

}  // namespace CB::feature
