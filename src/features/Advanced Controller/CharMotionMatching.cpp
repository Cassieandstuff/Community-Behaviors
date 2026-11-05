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

#include <Common/Base/hkBase.h>
#include <Behavior/Behavior/Context/hkbContext.h>
#include <Behavior/Behavior/Generator/Clip/hkbClipGenerator.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstddef>
#include <cstring>
#include <deque>
#include <format>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

/// @file
/// @brief Motion matching: CBPoseSearchGenerator's search, and the character's side of it.
/// @note  The node searches. In its update it reads this frame's inputs and query from its graph's variables, runs the
///        chooser and the character's Matcher over the database its own loaded data states, and writes what plays next to
///        the variables its graph binds, which the engine copies into it and its children. So the node is the whole of
///        it, and a character needs only to have those variables set.
/// @note  The character's side is the frame hook and the predictor, which set them. Only the player has one so far.
/// @note  The search runs on the job updating the character's graph, holding its animation graph manager's lock.
/// @note  THE ADD-ON CONTRACT (Motion Matching.hky, shipped apart from CB): the behavior `kGraphKey` whose root is a
///        CBPoseSearchGenerator over clip generators (MM_Clip = the child index; a clip listed in several databases is
///        one child per database), each child a row naming its database and carrying its flags and searched range,
///        `databases` with Unreal's cost biases (condition expressions), and `chooser` rows each holding a `when`
///        condition expression; the selected child bound to `kClipVar` and every child's user-controlled time to
///        `kTimeVar`; `kEnableVar`, declared in 0_master and mt_behavior, switches mt_behavior's LocomotionDefault to that
///        graph; and the inputs declared on it (compile/MotionMatchingBundle.h). Without the add-on the graph is not
///        served, nothing is built, and the controller runs exactly as before.

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

        constexpr std::string_view kGraphKey   = "meshes/actors/character/behaviors/motionmatching.hkx";
        constexpr std::string_view kProjectDir = "meshes/actors/character/";   ///< a clip's animationName is below this
        constexpr const char*      kEnableVar  = "MM_Enable";
        constexpr const char*      kClipVar    = "MM_Clip";
        constexpr const char*      kTimeVar    = "MM_Time";

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

        /// @brief The player's database: read by the frame hook and the predictor's Rate, lock-free.
        /// @note  The add-on's once it is warm, then whichever the player's node asks for.
        std::atomic<const Database*> g_db{ nullptr };

        /// @brief What plays: a clip and the time in it.
        struct Playback {
            int    clip = -1;
            double time = 0;
        };
        constexpr double kQueryStale = 0.25;   ///< seconds without a query from the predictor before there is none
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

        /// @brief Every database asked for, by the selector it was built from (DatabaseSpec::Key).
        /// @note  A node asks with what it loaded, so its database is the one its own graph states. Published entries
        ///        are never freed (process lifetime), so a slot's pointer outlives anything.
        struct Store {
            std::mutex                                       lock;
            std::optional<core::resolve::LoadOrderReader>    reader;
            std::map<std::string, std::unique_ptr<Database>> entries;
            std::deque<std::pair<std::string, ps::DatabaseSpec>> queue;
            bool                                             building = false;
            std::string                                      warmedKey;   ///< the add-on's, built ahead of its first activation
        };
        Store g_store;

        /// @brief Measure every clip `spec` plays and publish its database.
        /// @note Runs on the build thread: the add-on's authored YAML takes seconds to parse. Each distinct animationName
        ///       is measured once (a clip may be a child of several databases) and its rows are appended once per child
        ///       that plays it.
        void Build(const core::resolve::LoadOrderReader& reader, const std::string& key, const ps::DatabaseSpec& spec)
        {
            const auto t0 = Clock::now();
            /// @note The cache holds the first build's clips and whatever later ones add: only the first can tell a clip
            ///       is no longer played, and later ones are rare (a node the add-on did not ship).
            static std::map<std::string, ea::MmCachedClip> cache = LoadCache();
            static bool                                    first = true;
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
            auto db = ps::BuildDatabase(spec, clipOf, [](const std::string& message) { LOG_WARN("motion matching: {}", message); }, &report);

            if (first && fresh.size() != cache.size()) cacheDirty = true;
            if (first) cache.clear();
            first = false;
            const std::size_t played = fresh.size();
            for (auto& c : fresh) {
                std::string name = c.name;
                cache.insert_or_assign(std::move(name), std::move(c));
            }
            if (cacheDirty) {
                std::vector<ea::MmCachedClip> all;
                all.reserve(cache.size());
                for (const auto& [name, c] : cache) all.push_back(c);
                std::vector<std::uint8_t> bytes;
                std::string               err;
                std::error_code           ec;
                std::filesystem::create_directories(CachePath().parent_path(), ec);
                if (!ea::WriteMmCache(all, bytes, err) || !CB::engine::disk::WriteFile(CachePath(), bytes, {}, &err))
                    LOG_WARN("motion matching: cache not written ({})", err);
            }
            LOG_INFO("motion matching: {} of {} clip(s) from the cache", cacheHits, played);
            if (!db) {
                LOG_WARN("motion matching: no clip of a selector could be measured - that node follows its selected index");
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
            std::scoped_lock lock(g_store.lock);
            const Database* built = db.get();
            g_store.entries[key]  = std::move(db);
            if (key == g_store.warmedKey) g_db.store(built, std::memory_order_release);
        }

        /// @brief The build thread: one database at a time, then gone until the next is asked for.
        void BuildQueued()
        {
            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
            for (;;) {
                std::pair<std::string, ps::DatabaseSpec> job;
                core::resolve::LoadOrderReader           reader = [&] {
                    std::scoped_lock lock(g_store.lock);
                    if (g_store.queue.empty()) {
                        g_store.building = false;
                        return core::resolve::LoadOrderReader{};
                    }
                    job = std::move(g_store.queue.front());
                    g_store.queue.pop_front();
                    return *g_store.reader;
                }();
                if (job.first.empty()) return;
                Build(reader, job.first, job.second);
            }
        }

        /// @brief Build the add-on's database ahead of its first activation, so turning it on never waits for a measure.
        /// @note  A warm-up and nothing more: the node asks with what it loaded, which finds this one when the graph
        ///        served states the same selector, and builds its own when it does not.
        void Warm(const core::resolve::LoadOrderReader& reader)
        {
            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
            CB::engine::decompile::ParsedHky graph;
            if (!reader.mergedGraph(std::string(kGraphKey), graph)) {
                LOG_INFO("motion matching: '{}' is not in the load order (no Motion Matching.hky database bundle installed) - nothing to warm", kGraphKey);
                return;
            }
            const auto* selector = ps::RootSelector(graph);
            if (!selector) {
                LOG_WARN("motion matching: '{}' does not have a CBPoseSearchGenerator at its root - nothing to warm", kGraphKey);
                return;
            }
            const ps::DatabaseSpec spec = ps::SpecOf(*selector);
            {
                std::scoped_lock lock(g_store.lock);
                g_store.warmedKey = spec.Key();
            }
            controller::motion::DatabaseFor(spec);
        }

        [[maybe_unused]] const bool s_build = [] {
            if (!ch::motion::On()) return false;
            core::resolve::OnLoadOrderReady([](const core::resolve::LoadOrderReader& reader) {
                {
                    std::scoped_lock lock(g_store.lock);
                    g_store.reader = reader;
                }
                std::thread([reader] { Warm(reader); }).detach();
            });
            return true;
        }();

        /// @brief One node's matcher for one character, kept apart from the node as the dead blend is.
        struct MatchSlot {
            ps::Matcher        matcher;
            const Database*    db    = nullptr;
            float              retry = 0;   ///< seconds until asking for the database again
            Inputs             before = InitialInputs();
            relay::EvalContext ctx;
            std::vector<char>  lastActive;
            Clock::time_point  used{};
        };
        constexpr float  kDatabaseRetry = 1.0f;   ///< seconds between asking for a database not built yet
        constexpr double kSlotStale     = 0.5;    ///< seconds unused before a slot starts over
        constexpr double kSlotDrop      = 10.0;   ///< seconds unused before a slot is dropped

        /// @brief `node`'s slot: a clone is one character's, and a character may hold several nodes, each with its own database.
        /// @note  A character updates on one thread at a time and different ones update in parallel, so the table's lock
        ///        covers finding a slot and nothing done with it.
        std::shared_ptr<MatchSlot> SlotOf(const void* node, const RE::Actor*)
        {
            static std::mutex                                                lock;
            static std::unordered_map<const void*, std::shared_ptr<MatchSlot>> slots;
            static std::uint32_t                                             calls = 0;
            const auto       now = Clock::now();
            std::scoped_lock held(lock);
            auto&            slot = slots[node];
            if (!slot || std::chrono::duration<double>(now - slot->used).count() > kSlotStale) slot = std::make_shared<MatchSlot>();
            slot->used = now;
            std::shared_ptr<MatchSlot> out = slot;
            if (++calls % 600 == 0)
                std::erase_if(slots, [&](const auto& kv) { return std::chrono::duration<double>(now - kv.second->used).count() > kSlotDrop; });
            return out;
        }

        /// @return the actor whose graph holds `character`, null when it is not an actor's
        /// @note   context.m_character is the graph's own characterInstance (CharSkeletonWatch compares them).
        RE::Actor* ActorOf(const ::hkbCharacter* character)
        {
            const auto* graph = reinterpret_cast<const RE::BShkbAnimationGraph*>(reinterpret_cast<const char*>(character) -
                                                                                 offsetof(RE::BShkbAnimationGraph, characterInstance));
            RE::Actor*  actor = graph->holder;
            return actor && actor->Is(RE::FormType::ActorCharacter) ? actor : nullptr;
        }

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

        /// @brief One character's side of the matching, by FormID: what the predictor, the frame hook and the node hand each other.
        /// @note  Entries stay, a small one per character met.
        struct CharacterMatch {
            /// @brief The node's current clip, for the frame hook and Describe.
            std::mutex        shownLock;
            Playback          shown;
            /// @brief The predictor's latest query, which the frame hook turns into graph variables.
            std::mutex        queryLock;
            ch::motion::Query query;
            bool              queryValid = false;
            Clock::time_point queryAt{};
            /// @brief The playback rate (clip seconds per second): written by the predictor's Rate, read by the frame hook.
            std::atomic<float> rate{ 1.0f };
            /// @brief The predictor's landing time (SetTimeToLand), read by the frame hook's MM_TimeToLand while airborne.
            std::atomic<float> timeToLand{ controller::motion::kNoLanding };
            /// @brief The frame hook's alone: the inputs as of the previous frame, what they remember, and whether it switched the graph on.
            Inputs            inputs = InitialInputs();
            InputMemory       memory;
            bool              enabled = false;
        };

        struct CharacterTable {
            std::mutex                                                 lock;
            std::unordered_map<RE::FormID, std::unique_ptr<CharacterMatch>> byActor;
        };

        CharacterMatch& MatchOf(RE::FormID actor)
        {
            static CharacterTable t;
            std::scoped_lock lock(t.lock);
            std::unique_ptr<CharacterMatch>& match = t.byActor[actor];
            if (!match) match = std::make_unique<CharacterMatch>();
            return *match;
        }

        /// @brief GASP's chooser inputs for this frame into `io` (its previous values on entry).
        /// @param playingPivots the playing child's database is tagged "Pivots".
        /// @return false without a fresh movement-model frame: `io` keeps its values.
        bool ComputeInputs(RE::Actor* player, CharacterMatch& match, float dt, bool playingPivots, Inputs& io)
        {
            InputMemory& m = match.memory;
            const float z  = player->GetPosition().z;
            const float vz = m.haveZ && dt > 0.0f ? (z - m.lastZ) / dt : 0.0f;
            m.lastZ = z, m.haveZ = true;

            mv::Snapshot snap;
            if (!mv::Current(player->GetFormID(), snap)) return false;
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
            /// @note Held ps::kSearchInterval: GASP searches every tick, CB's search runs at most that often, and a one-frame
            ///       input would mostly miss it.
            float intent = s.yaw;
            if (strafe) {
                const float dx = in.targetX - snap.px, dy = in.targetY - snap.py;
                if (in.haveTarget && dx * dx + dy * dy > 1.0f) intent = std::atan2(dx, dy);
            } else if (m.haveStickYaw) {
                intent = m.stickYaw;
            }
            if (wasMoving && !moving && std::fabs(ch::WrapPi(intent - s.yaw)) >= kTurnInPlace) m.turnHold = static_cast<float>(ps::kSearchInterval);
            else m.turnHold = (std::max)(0.0f, m.turnHold - dt);
            io[kShouldTurnInPlace] = m.turnHold > 0.0f;

            io[kShouldSpinTransition] = !playingPivots && speed >= kSpinSpeed && std::fabs(ch::WrapPi(velYaw - s.yaw)) >= kSpinAngle;
            io[kJustTraversed]        = 0;
            io[kDatabaseLOD]          = DatabaseLod();
            io.real[kSpeed2D]         = speed;
            io.real[kTimeToLand]      = in.airborne ? match.timeToLand.load(std::memory_order_relaxed) : 0.0f;
            return true;
        }

        const std::array<RE::BSFixedString, kInputs>& InputNames()
        {
            static const std::array<RE::BSFixedString, kInputs> names = [] {
                std::array<RE::BSFixedString, kInputs> a;
                for (int i = 0; i < kInputs; ++i) a[i] = kInputNames[i];
                return a;
            }();
            return names;
        }

        const std::array<RE::BSFixedString, kRealInputs>& RealInputNames()
        {
            static const std::array<RE::BSFixedString, kRealInputs> names = [] {
                std::array<RE::BSFixedString, kRealInputs> a;
                for (int i = 0; i < kRealInputs; ++i) a[i] = kRealInputNames[i];
                return a;
            }();
            return names;
        }

        void WriteInputs(RE::Actor* player, const Inputs& in)
        {
            const auto& names = InputNames();
            for (int i = 0; i < kInputs; ++i) {
                if (IsBoolInput(i)) player->SetGraphVariableBool(names[i], in[i] != 0);
                else player->SetGraphVariableInt(names[i], in[i]);
            }
            const auto& realNames = RealInputNames();
            for (int i = 0; i < kRealInputs; ++i) player->SetGraphVariableFloat(realNames[i], in.real[i]);
        }

        /// @brief The chooser's inputs as `actor`'s graph holds them; one it cannot read keeps its value in `in`.
        void ReadInputs(RE::Actor* actor, Inputs& in)
        {
            const auto& names = InputNames();
            for (int i = 0; i < kInputs; ++i) {
                if (IsBoolInput(i)) {
                    bool b = false;
                    if (actor->GetGraphVariableBool(names[i], b)) in[i] = b ? 1 : 0;
                } else {
                    actor->GetGraphVariableInt(names[i], in[i]);
                }
            }
            const auto& realNames = RealInputNames();
            for (int i = 0; i < kRealInputs; ++i) actor->GetGraphVariableFloat(realNames[i], in.real[i]);
        }

        /// @brief The query's graph variables.
        struct QueryNames {
            RE::BSFixedString have{ "MM_HaveQuery" }, rate{ "MM_PlayRate" }, desired{ "MM_Desired" };
            std::array<RE::BSFixedString, 3> future{ RE::BSFixedString("MM_Future0"), RE::BSFixedString("MM_Future1"), RE::BSFixedString("MM_Future2") };
            std::array<RE::BSFixedString, 3> past{ RE::BSFixedString("MM_Past0"), RE::BSFixedString("MM_Past1"), RE::BSFixedString("MM_Past2") };
        };
        const QueryNames& QueryVariables()
        {
            static const QueryNames names;
            return names;
        }

        /// @brief The predictor's latest query as the character's graph variables, and the playback rate.
        /// @return whether there is a query: the predictor's stopping (it stepped no query for kQueryStale) is none.
        bool WriteQuery(RE::Actor* player, CharacterMatch& match)
        {
            const QueryNames& names = QueryVariables();
            player->SetGraphVariableFloat(names.rate, match.rate.load(std::memory_order_relaxed));
            ch::motion::Query q;
            bool              have = false;
            {
                std::scoped_lock lock(match.queryLock);
                have = match.queryValid && std::chrono::duration<double>(Clock::now() - match.queryAt).count() < kQueryStale;
                q    = match.query;
            }
            player->SetGraphVariableBool(names.have, have);
            if (!have) return false;
            RE::NiPoint3 p;
            for (std::size_t k = 0; k < 3; ++k) {
                p = { q.x[k], q.y[k], std::atan2(-q.dirX[k], q.dirY[k]) };
                player->SetGraphVariableNiPoint3(names.future[k], p);
                p = { q.pastX[k], q.pastY[k], std::atan2(-q.pastDirX[k], q.pastDirY[k]) };
                player->SetGraphVariableNiPoint3(names.past[k], p);
            }
            p = { q.desiredX, q.desiredY, 0.0f };
            player->SetGraphVariableNiPoint3(names.desired, p);
            return true;
        }

        /// @brief The query and playback rate as `actor`'s graph holds them.
        void ReadQuery(RE::Actor* actor, ps::MatchRequest& out)
        {
            const QueryNames& names = QueryVariables();
            float             rate  = 0.0f;
            /// @note A graph just activated may hold its declared 0.
            if (actor->GetGraphVariableFloat(names.rate, rate) && rate > 0.0f) out.rate = rate;
            bool have = false;
            if (!actor->GetGraphVariableBool(names.have, have) || !have) return;
            RE::NiPoint3 p;
            for (std::size_t k = 0; k < 3; ++k) {
                if (!actor->GetGraphVariableNiPoint3(names.future[k], p)) return;
                out.x[k] = p.x, out.y[k] = p.y, out.dirX[k] = -std::sin(p.z), out.dirY[k] = std::cos(p.z);
                if (!actor->GetGraphVariableNiPoint3(names.past[k], p)) return;
                out.pastX[k] = p.x, out.pastY[k] = p.y, out.pastDirX[k] = -std::sin(p.z), out.pastDirY[k] = std::cos(p.z);
            }
            if (!actor->GetGraphVariableNiPoint3(names.desired, p)) return;
            out.desiredX = p.x, out.desiredY = p.y;
            out.haveQuery = true;
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

        /// @brief The character's side of motion matching: everything the node reads, set as the graph's variables each
        ///        frame, as the engine hands the graph its `Speed`.
        /// @note  Only the player so far: the inputs' memory here and the predictor's query are the player's alone.
        /// @brief An NPC whose graph holds the add-on, the frame hook having no other way to tell: an NPC of a mod's own
        ///        behavior project holds no MM_Enable.
        RE::Actor* MatchingNpcOf(RE::MovementControllerNPC* controller)
        {
            static const bool on = core::debug::FlagOn("npc_controller");
            RE::Actor* actor = on && controller ? controller->actor : nullptr;
            if (!actor || actor == RE::PlayerCharacter::GetSingleton() || !actor->Is(RE::FormType::ActorCharacter)) return nullptr;
            const auto& mc = actor->GetActorRuntimeData().movementController;
            if (!mc || static_cast<void*>(mc.get()) != controller) return nullptr;
            static const RE::BSFixedString enableName(kEnableVar);
            std::int32_t enable = 0;
            return actor->GetGraphVariableInt(enableName, enable) ? actor : nullptr;
        }

        void OnFrame(Frame::Next next, RE::MovementControllerNPC* controller, float deltaTime, game::MovementFrame* out)
        {
            next(controller, deltaTime, out);
            RE::Actor*       player = out ? PlayerOf(controller) : nullptr;
            RE::Actor*       actor  = player ? player : out ? MatchingNpcOf(controller) : nullptr;
            const Database*  db     = g_db.load(std::memory_order_acquire);
            if (!actor) return;
            if (player) ch::motion::QueueSkeletonWatch();
            if (!db) return;
            if (player) CountFrame();
            static const RE::BSFixedString enableName(kEnableVar);
            CharacterMatch& match = MatchOf(actor->GetFormID());
            /// @note An NPC its frame driver has let go (it sat down, mounted, died) goes back to the graph's own locomotion.
            const auto* character = reinterpret_cast<const ::bhkCharacterController*>(actor->GetCharController());
            if (!player && !ch::IsManagedNpc(character)) {
                if (match.enabled) actor->SetGraphVariableInt(enableName, 0), match.enabled = false;
                return;
            }
            if (player) CB_PLOT("MM rate", match.rate.load(std::memory_order_relaxed));

            Playback p;
            {
                std::scoped_lock lock(match.shownLock);
                p = match.shown;
            }
            const int playingDb = p.clip >= 0 ? db->clips[p.clip].database : -1;
            ComputeInputs(actor, match, deltaTime, playingDb >= 0 && db->databases[playingDb].pivots, match.inputs);
            WriteInputs(actor, match.inputs);

            /// @note Until there is a query to search with, MM_Enable stays at its declared 0: vanilla locomotion plays.
            if (!WriteQuery(actor, match)) return;
            actor->SetGraphVariableInt(enableName, 1);
            match.enabled = true;
        }

        [[maybe_unused]] const bool s_subscribed =
            Frame::Subscribe(&OnFrame, hooks::Priority::Normal, "CharMotionMatching", "char_motion_matching");

        /// @brief How often clips are sampled: the proof that CBPoseSearchGenerator's skipped children are not.
        /// @note  Kept every child active, so a skip the engine ignored would sample all 252 add-on clips every frame.
        using ClipGenerate = hooks::Override<&::hkbClipGenerator::generate>;
        constexpr std::string_view kClipPrefix = "animations\\MotionMatching\\";   ///< the add-on's clips, as authored
        constexpr double           kReportEvery = 5.0;   ///< seconds
        struct GenerateCount {
            std::atomic<std::uint64_t> ours{ 0 }, all{ 0 }, frames{ 0 };
            std::atomic<std::uint64_t> oursThisFrame{ 0 };   ///< add-on clip samples since the last player frame (the plot)
        };
        GenerateCount g_generates;

        void OnClipGenerate(ClipGenerate::Next next, ::hkbClipGenerator* clip, const ::hkbContext& context,
                            const ::hkbGeneratorOutput** activeChildrenOutput, ::hkbGeneratorOutput& output,
                            ::hkReal timeOffset)
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
                         ReselectTime(), ps::kForceSpeed, DatabaseLod());
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

    void controller::motion::SetTimeToLand(RE::FormID actor, float seconds) { MatchOf(actor).timeToLand.store(seconds, std::memory_order_relaxed); }

    std::string controller::motion::Describe()
    {
        const Database* db = g_db.load(std::memory_order_acquire);
        if (!db) return "no database";
        CharacterMatch& match = MatchOf(ch::move::kPlayerId);
        Playback        p;
        {
            std::scoped_lock lock(match.shownLock);
            p = match.shown;
        }
        if (p.clip < 0) return "no clip chosen yet";
        const Clip&      c    = db->clips[p.clip];
        const Clock::rep at   = g_lastSwitch.load(std::memory_order_relaxed);
        const double     ago  = at ? std::chrono::duration<double>(Clock::now() - Clock::time_point(Clock::duration(at))).count() : -1.0;
        return std::format("clip {} '{}' ({}) frame {}, blend left {:.2f} s, last switch {:.2f} s ago", p.clip, c.name,
                           c.database >= 0 ? db->databases[c.database].name : std::string("no database"),
                           c.frames > 0 ? FrameOf(c, p.time) : 0, BlendRemaining(), ago);
    }

    float controller::motion::Rate(RE::FormID actor, const FrameSpeed* frame)
    {
        CharacterMatch& match = MatchOf(actor);
        const Database* db    = g_db.load(std::memory_order_acquire);
        const float     last  = match.rate.load(std::memory_order_relaxed);
        if (!db || !frame || frame->gait == Gait::None || frame->full < kRateStill) return last;
        const float clipSpeed = db->gaitSpeed[static_cast<std::size_t>(frame->gait)];
        if (clipSpeed <= 0.0f) return last;
        const float rate = std::clamp(frame->full / clipSpeed, kMinRate, kMaxRate);
        match.rate.store(rate, std::memory_order_relaxed);
        return rate;
    }

    void controller::motion::Publish(RE::FormID actor, const Query& q)
    {
        CharacterMatch&  match = MatchOf(actor);
        std::scoped_lock lock(match.queryLock);
        match.query      = q;
        match.queryValid = true;
        match.queryAt    = Clock::now();
    }

    void controller::motion::PublishNoQuery(RE::FormID actor)
    {
        CharacterMatch&  match = MatchOf(actor);
        std::scoped_lock lock(match.queryLock);
        match.queryValid = false;
    }

    int controller::motion::RunMatcher(const ::hkbContext& context, PoseSearchGenerator& node, float timestep, bool restart)
    {
        if (!On()) return -1;
        RE::Actor* actor = ActorOf(context.m_character);
        if (!actor) return -1;
        const std::shared_ptr<MatchSlot> held = SlotOf(&node, actor);
        MatchSlot&                       s    = *held;
        if (!s.db) {
            s.retry -= timestep;
            if (s.retry > 0.0f) return -1;
            s.retry = kDatabaseRetry;
            std::optional<ps::DatabaseSpec> spec = node.Describe(context);
            if (!spec) {
                static std::atomic<bool> warned{ false };
                if (!warned.exchange(true)) LOG_WARN("motion matching: a CBPoseSearchGenerator clone has no template - it follows its selected index");
                return -1;
            }
            s.db = DatabaseFor(*spec);
            if (!s.db) return -1;
            s.matcher.Restart();
            static std::atomic<int> searchers{ 0 };
            if (searchers.fetch_add(1, std::memory_order_relaxed) < 32)
                LOG_INFO("motion matching: '{}' searches {} child(ren) of a database built for its own data", actor->GetName(), s.db->clips.size());
        }
        const Database& db       = *s.db;
        const bool      isPlayer = actor == RE::PlayerCharacter::GetSingleton();
        if (isPlayer) g_db.store(&db, std::memory_order_release);
        if (restart) s.matcher.Restart();
        CB_ZONE(zone, "MM node");

        /// @note Everything the node asks of the character arrives as the graph's variables, which whoever drives it set.
        Inputs in = InitialInputs();
        ReadInputs(actor, in);
        const bool       inputsChanged = InputsInterrupt(s.before, in);
        s.before                       = in;
        ps::MatchRequest request;
        ReadQuery(actor, request);
        request.inputsChanged = inputsChanged;
        request.reselectTime  = ReselectTime();
        const InputVariables vars(in);
        s.ctx.Reset(actor);
        s.ctx.variables = &vars;
        request.active  = ps::Choose(db, s.ctx);
        request.biases  = ps::EvaluateBiases(db, s.ctx);

        const ps::MatchStep step = s.matcher.Update(db, request, timestep);

        if (request.active != s.lastActive) {
            static std::atomic<int> changes{ 0 };
            /// @note The log's lines are the player's: every NPC's would spend them all.
            if (isPlayer && changes.fetch_add(1, std::memory_order_relaxed) < 300) {
                const int playing = step.clip >= 0 ? db.clips[step.clip].database : -1;
                LOG_INFO("motion matching: searching {} ({}){}", ActiveNames(db, request.active), InputsText(in),
                         step.searched && std::string_view(step.trigger) == "interrupt" && playing >= 0
                             ? std::format(" - interrupts '{}'", db.databases[playing].name)
                             : std::string());
            }
            s.lastActive = request.active;
        }

        const auto report = [&] {
            const ps::SearchResult& r = step.result;
            CB_PLOT("MM search ms", step.searchMs);
            {
                std::scoped_lock lock(g_costs.lock);
                if (r.bestRow >= 0 && g_costs.winning.size() < kCostLogCap) g_costs.winning.push_back(r.best);
                if (r.playingRow >= 0 && g_costs.playing.size() < kCostLogCap) g_costs.playing.push_back(r.playing);
            }
            static std::atomic<int> switches{ 0 };
            if (step.switched) {
                const Clip& c      = db.clips[step.nextClip];
                const char* dbName = c.database >= 0 ? db.databases[c.database].name.c_str() : "-";
                const int   frame  = r.bestRow - c.first;
                const int   count  = switches.fetch_add(1, std::memory_order_relaxed) + 1;
                if (isPlayer) g_lastSwitch.store(Clock::now().time_since_epoch().count(), std::memory_order_relaxed);
                CB_PLOT("MM switches", count);
                CB_MESSAGE("MM -> clip {} '{}' [{}] frame {} ({}, cost {:.2f}, was {:.2f})", step.nextClip, c.name, dbName, frame, step.trigger,
                           r.best, r.playing);
                static constexpr const char* kGaits[3] = { "walk", "run", "sprint" };
                static constexpr const char* kFrameGaits[4] = { "none", "walk", "run", "sprint" };
                if (LogBudget())
                    LOG_INFO("motion matching: -> clip {} '{}' in '{}' frame {} x{:.2f} (cost {:.2f}, was {:.2f}; {}; gait {} / frame {}; "
                             "{} of {} rows in {:.2f} ms)",
                             step.nextClip, c.name, dbName, frame, request.rate, r.best, r.playing, step.trigger, kGaits[std::clamp(in[kGait], 0, 2)],
                             kFrameGaits[db.rowGait[r.bestRow] & 3], r.searched, db.clipOf.size(), step.searchMs);
            }
            if (!isPlayer || !ch::hud::On()) return;
            ch::hud::Matching h;
            const int   bestRow = step.switched ? r.bestRow : r.playingRow;
            const float best    = step.switched ? r.best : r.playing;
            const int   shown   = bestRow >= 0 ? db.clipOf[bestRow] : -1;
            if (shown >= 0) {
                const Clip&        c     = db.clips[shown];
                const auto         slash = c.name.find_last_of('\\');
                const std::string  stem  = slash == std::string::npos ? c.name : c.name.substr(slash + 1);
                std::snprintf(h.clip, sizeof(h.clip), "%s", stem.c_str());
                std::snprintf(h.database, sizeof(h.database), "%s", c.database >= 0 ? db.databases[c.database].name.c_str() : "-");
                h.frame = bestRow - c.first;
            }
            std::snprintf(h.active, sizeof(h.active), "%s", ActiveNames(db, step.searchedActive).c_str());
            std::snprintf(h.chooser, sizeof(h.chooser), "%s", InputsText(in).c_str());
            h.cost     = best;
            h.searchMs = step.searchMs;
            h.switches = switches.load(std::memory_order_relaxed);
            h.rate     = request.rate;
            h.blend    = BlendRemaining();
            ch::hud::Publish(h);
        };
        if (step.searched && isPlayer) report();

        {
            CharacterMatch&  match = MatchOf(actor->GetFormID());
            std::scoped_lock lock(match.shownLock);
            match.shown = { step.clip, step.time };
        }
        if (step.nextClip < 0) return -1;
        /// @note Through the graph's own bindings: the engine copies these into this node and every child before their
        ///       next updates, per character. A child updates before its parent, so anything this update wrote straight
        ///       into a child would reach the pose a frame later anyway.
        const Clip&  next     = db.clips[step.nextClip];
        const float  fraction = static_cast<float>(next.duration > 0 ? step.nextTime / next.duration : 0.0);
        if (node.m_variableBindingSet == HK_NULL) {
            node.PlayUnbound(context, step.nextClip, fraction);
            return step.nextClip;
        }
        static const RE::BSFixedString clipName(kClipVar), timeName(kTimeVar);
        actor->SetGraphVariableInt(clipName, step.nextClip);
        actor->SetGraphVariableFloat(timeName, fraction);
        return step.nextClip;
    }

    const ps::Database* controller::motion::DatabaseFor(const ps::DatabaseSpec& spec)
    {
        std::string key = spec.Key();
        std::scoped_lock lock(g_store.lock);
        if (const auto it = g_store.entries.find(key); it != g_store.entries.end()) return it->second.get();
        if (!g_store.reader) return nullptr;
        g_store.entries.emplace(key, nullptr);
        g_store.queue.push_back({ std::move(key), spec });
        if (!g_store.building) {
            g_store.building = true;
            std::thread(BuildQueued).detach();
        }
        return nullptr;
    }

}  // namespace CB::feature
