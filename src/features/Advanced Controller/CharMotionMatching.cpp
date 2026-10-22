#include "PCH.h"

#include "CharDebugHud.h"
#include "CharGround.h"
#include "CharMotionMatching.h"
#include "CharPoseSearch.h"
#include "CharSkeletonWatch.h"

#include <core/debug/Settings.h>
#include <core/resolve/LoadOrderReader.h>
#include <codec/format/AnimationYamlLoader.h>
#include <compile/MotionFeatures.h>
#include <platform/game/Movement.h>
#include <platform/hooks/Override.h>
#include <platform/profile/Profile.h>

#include <Behavior/Generator/Clip/hkbClipGenerator.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
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
///        built off-thread once the load order is read; a brute-force nearest-frame search over it; and the winner
///        played through the add-on's graph.
/// @note  THE ADD-ON CONTRACT (Motion Matching.hky, shipped apart from CB): the behavior `kGraphKey` whose root is a
///        CBPoseSearchGenerator over one clip generator per animation (child index = database clip index), the selected
///        child bound to `kClipVar` and every child's user-controlled time to `kTimeVar`; `kEnableVar`, declared in
///        0_master and mt_behavior, switches mt_behavior's LocomotionDefault to that graph. Without the add-on the
///        graph is not served, nothing is built, and the controller runs exactly as before.

namespace CB::feature {

    namespace {

        namespace hooks = platform::hooks;
        namespace ch    = CB::feature::controller;
        namespace game  = CB::platform::game;
        namespace ea    = CB::engine::anim;
        namespace F     = CB::engine::anim::features;
        using Frame     = hooks::Override<&game::MovementControllerNPC_ComputeFrameMovement>;
        using Clock     = std::chrono::steady_clock;

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
        constexpr float  kForceMoving       = 10.0f;       ///< game units/s: a direction counts only above this speed

        /// @brief The search's switching tuning, read once from settings.ini [Character].
        /// @note  Why the margin: in one run 145 of 399 switches gained under 15%, each a needless blend; why the ban: 12%
        ///        of switches went A -> B -> A within a second. A forced search keeps both, so input noise cannot flip.
        struct SwitchTuning {
            float margin   = 0.15f;   ///< fMMSwitchMargin: a switch must beat the current frame's cost by this fraction
            float reselect = 1.0f;    ///< fMMReselectTime: seconds a pick's neighbourhood stays banned
        };
        const SwitchTuning& Switching()
        {
            static const SwitchTuning t{ std::clamp(ch::SettingFloat("fMMSwitchMargin", 0.15f), 0.0f, 0.9f),
                                         std::clamp(ch::SettingFloat("fMMReselectTime", 1.0f), 0.0f, 5.0f) };
            return t;
        }

        /// @brief How many searches found a better candidate the margin held back, for the periodic report.
        std::atomic<std::uint64_t> g_held{ 0 };
        constexpr int    kIgnoreSurrounding = 20;    ///< frames: a candidate this close in the current clip is the current
        constexpr double kEndMargin         = 0.1;   ///< seconds before a clip's end that force a search

        /// @brief Each feature group's weight (Holden's demo defaults).
        /// @note  Applied at search time, never stored in the database, so a weight change needs no rebuild.
        /// @note  The past groups start at the future's weights.
        constexpr float kWeightTrajPos = 1.0f, kWeightTrajDir = 1.5f, kWeightPastPos = 1.0f, kWeightPastDir = 1.5f,
                        kWeightFootPos = 0.75f, kWeightFootVel = 1.0f, kWeightHipVel = 1.0f;
        constexpr int   kGroups = 7;

        /// @brief The playback rate's range: a clip plays at most this much slower or faster than authored; past it the
        ///        feet slide rather than the animation visibly slowing or fast-forwarding.
        constexpr float kMinRate = 0.7f, kMaxRate = 1.5f;
        /// @brief Below this asked-for speed (game units/s) the gait's rate stays as it was: standing has no speed to match.
        constexpr float kRateStill = 10.0f;

        struct Clip {
            std::string name;            ///< the clip generator's animationName
            int         first    = 0;    ///< its first row
            int         frames   = 0;    ///< 0 = the clip could not be measured (its index stays, never chosen)
            double      dt       = 1.0 / 30.0;
            double      duration = 0;    ///< what the user-controlled time fraction spans
        };

        /// @brief The database: one row of kDims floats per clip frame, normalized (mean, per-group spread) but unweighted.
        struct Database {
            std::vector<Clip>               clips;
            std::vector<float>              rows;
            std::vector<int>                clipOf;       ///< row -> clip
            std::vector<char>               searchable;   ///< row's whole future lies inside its clip
            std::vector<std::uint8_t>       rowTags;      ///< the frame's kTag* bits (the search's gates)
            std::vector<std::uint8_t>       rowGait;      ///< the frame's gait tag as a ch::Gait (None for Idle or none)
            std::array<float, F::kDims>     mean{}, scale{}, weight{};
            /// @brief The clips' own speed per gait (game units/s, indexed by ch::Gait), 0 where no clip has one: the
            ///        median root speed of the frames tagged that gait and Forward, neither Accelerate nor Decelerate.
            std::array<float, 4>            gaitSpeed{};
        };
        /// @note Published once, never freed (process lifetime): the search and the frame hook read it lock-free.
        std::atomic<const Database*> g_db{ nullptr };

        /// @brief What plays: a clip and the time in it.
        struct Playback {
            int    clip = -1;
            double time = 0;
        };
        struct Shared {
            std::mutex lock;
            Playback   now;
            Playback   pending;
            bool       havePending = false;
            bool       searchNow   = true;
        };
        Shared             g_shared;
        /// @brief The playback rate (clip seconds per second): written by the predictor's Rate, read by the frame hook.
        std::atomic<float> g_rate{ 1.0f };
        /// @brief When the last switch was chosen (steady_clock ticks; 0 = none yet), for Describe.
        std::atomic<Clock::rep> g_lastSwitch{ 0 };
        std::atomic<int>   g_logs{ 0 };
        bool LogBudget() { return g_logs.fetch_add(1, std::memory_order_relaxed) < 400; }

        int GroupOf(int d)
        {
            if (d < F::kTrajDir) return 0;
            if (d < F::kPastPos) return 1;
            if (d < F::kPastDir) return 2;
            if (d < F::kFootPos) return 3;
            if (d < F::kFootVel) return 4;
            if (d < F::kHipVel) return 5;
            return 6;
        }

        /// @brief The add-on graph's root selector, or null when the graph is not the contract's shape.
        CB::engine::codec::io::SchemaObject* RootSelector(const CB::engine::decompile::ParsedHky& g)
        {
            using CB::engine::codec::io::SchemaObject;
            if (!g.root) return nullptr;
            for (const auto& nv : g.root->Children("namedVariants")) {
                const auto* v     = dynamic_cast<const SchemaObject*>(nv.get());
                auto*       graph = v ? v->Child("variant") : nullptr;
                if (!graph || std::string_view(graph->ClassName()) != "hkbBehaviorGraph") continue;
                auto* gen = graph->Child("rootGenerator");
                return gen && std::string_view(gen->ClassName()) == "CBPoseSearchGenerator" ? gen : nullptr;
            }
            return nullptr;
        }

        /// @brief The per-frame tag bits the search gates on.
        constexpr std::uint8_t kTagIdle = 1, kTagAccelerate = 2, kTagDecelerate = 4;
        constexpr std::uint8_t kTagEnter = 8;   ///< the frame may be cut into (the clip's `entry` allows it)

        /// @brief Append `m`'s rows' tag bits and gait tags. Untagged clips get no bits: enterable anywhere, never a
        ///        standing candidate. Tag names compared byte-exact, as autotag writes them.
        void AppendRowTags(const ea::ClipMotion& m, const ea::AnimationDef& def, Database& db)
        {
            static const std::map<std::string, std::vector<std::pair<int, int>>> kNoTags;
            const auto& tags = def.tags ? *def.tags : kNoTags;
            const auto  in   = [&](const char* name, int i) {
                const auto it = tags.find(name);
                if (it == tags.end()) return false;
                for (const auto& [a, b] : it->second)
                    if (a <= i && i < b) return true;
                return false;
            };
            const bool changesOnly = def.entry == ea::AnimationDef::Entry::Changes;
            for (int i = 0; i < m.frames; i++) {
                std::uint8_t bits = 0;
                if (in("Idle", i)) bits |= kTagIdle;
                if (in("Accelerate", i)) bits |= kTagAccelerate;
                if (in("Decelerate", i)) bits |= kTagDecelerate;
                if (!changesOnly || (bits & (kTagAccelerate | kTagDecelerate))) bits |= kTagEnter;
                const ch::Gait gait = in("Sprint", i) ? ch::Gait::Sprint
                                    : in("Run", i)    ? ch::Gait::Run
                                    : in("Walk", i)   ? ch::Gait::Walk
                                                      : ch::Gait::None;
                db.rowTags.push_back(bits);
                db.rowGait.push_back(static_cast<std::uint8_t>(gait));
            }
        }

        /// @brief Add `m`'s steady forward frames' horizontal root speeds to their gait's list (see Database::gaitSpeed).
        /// @note  Tag names compared byte-exact, as autotag writes them.
        void CollectGaitSpeeds(const ea::ClipMotion& m, const std::map<std::string, std::vector<std::pair<int, int>>>& tags,
                               std::array<std::vector<float>, 4>& out)
        {
            const auto mark = [&](const char* name) {
                std::vector<char> on(static_cast<std::size_t>(m.frames), 0);
                if (const auto it = tags.find(name); it != tags.end())
                    for (const auto& [a, b] : it->second)
                        for (int i = (std::max)(a, 0); i < (std::min)(b, m.frames); ++i) on[i] = 1;
                return on;
            };
            const auto forward = mark("Forward"), accel = mark("Accelerate"), decel = mark("Decelerate");
            static constexpr const char* kGaitTags[4] = { nullptr, "Walk", "Run", "Sprint" };
            for (std::size_t g = 1; g < out.size(); ++g) {
                const auto on = mark(kGaitTags[g]);
                for (int i = 0; i + 1 < m.frames; ++i) {
                    if (!on[i] || !forward[i] || accel[i] || decel[i]) continue;
                    const double dx = m.rootPos[i + 1].x - m.rootPos[i].x, dy = m.rootPos[i + 1].y - m.rootPos[i].y;
                    out[g].push_back(static_cast<float>(std::sqrt(dx * dx + dy * dy) / m.dt));
                }
            }
        }

        /// @brief Measure every clip the add-on graph plays and publish the database.
        /// @note Runs on its own below-normal thread: 252 clips of authored YAML take seconds to parse.
        void Build(const core::resolve::LoadOrderReader& reader)
        {
            SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
            const auto t0 = Clock::now();
            CB::engine::decompile::ParsedHky graph;
            if (!reader.mergedGraph(std::string(kGraphKey), graph)) {
                LOG_INFO("motion matching: '{}' is not in the load order (the Motion Matching add-on is not installed) - off", kGraphKey);
                return;
            }
            auto* selector = RootSelector(graph);
            if (!selector) {
                LOG_WARN("motion matching: '{}' does not have a CBPoseSearchGenerator at its root - off", kGraphKey);
                return;
            }

            auto db = std::make_unique<Database>();
            int  measured = 0;
            std::array<std::vector<float>, 4> gaitSpeeds;
            for (const auto& child : selector->Children("generators")) {
                Clip c;
                c.first         = static_cast<int>(db->rows.size() / F::kDims);
                const auto* gen = dynamic_cast<const CB::engine::codec::io::SchemaObject*>(child.get());
                if (!gen || std::string_view(gen->ClassName()) != "hkbClipGenerator") {
                    LOG_WARN("motion matching: selector child {} is not a clip generator - never chosen", db->clips.size());
                    db->clips.push_back(c);
                    continue;
                }
                c.name           = gen->Str("animationName");
                std::string path = std::string(kProjectDir) + c.name;
                std::replace(path.begin(), path.end(), '\\', '/');
                try {
                    const auto text = reader.nativeAnimation(path);
                    if (!text) throw std::runtime_error("not shipped by any bundle");
                    const auto  def = ea::AnimationYamlLoader::LoadFromString(*text, path);
                    std::string err;
                    const auto  m = ea::MeasureClip(def, err);
                    if (!m) throw std::runtime_error(err);
                    if (!m->hasFeet || !m->hasHip) throw std::runtime_error("no humanoid leg chains");
                    ea::AppendClipFeatures(*m, db->rows);
                    c.frames   = m->frames;
                    c.dt       = m->dt;
                    c.duration = def.duration > 0 ? def.duration : (m->frames - 1) * m->dt;
                    const int horizon = static_cast<int>(std::lround(F::kHorizons.back() / m->dt));
                    for (int i = 0; i < m->frames; ++i) {
                        db->clipOf.push_back(static_cast<int>(db->clips.size()));
                        db->searchable.push_back(i + horizon <= m->frames - 1);
                    }
                    if (def.tags) CollectGaitSpeeds(*m, *def.tags, gaitSpeeds);
                    AppendRowTags(*m, def, *db);
                    ++measured;
                } catch (const std::exception& e) {
                    LOG_WARN("motion matching: clip {} '{}' not measured ({}) - never chosen", db->clips.size(), c.name, e.what());
                }
                db->clips.push_back(std::move(c));
            }
            const std::size_t n = db->rows.size() / F::kDims;
            if (n == 0) {
                LOG_WARN("motion matching: no clip of '{}' could be measured - off", kGraphKey);
                return;
            }

            /// @note Holden's normalization: each dimension centred on its mean, then each GROUP divided by its mean
            ///       spread, so a group's dimensions keep their relative sizes and every group weighs in comparably.
            std::array<double, F::kDims> mean{}, var{};
            for (std::size_t r = 0; r < n; ++r)
                for (int d = 0; d < F::kDims; ++d) mean[d] += db->rows[r * F::kDims + d];
            for (auto& m : mean) m /= static_cast<double>(n);
            for (std::size_t r = 0; r < n; ++r)
                for (int d = 0; d < F::kDims; ++d) {
                    const double x = db->rows[r * F::kDims + d] - mean[d];
                    var[d] += x * x;
                }
            std::array<double, kGroups> groupSpread{}, groupDims{};
            for (int d = 0; d < F::kDims; ++d) {
                groupSpread[GroupOf(d)] += std::sqrt(var[d] / static_cast<double>(n));
                groupDims[GroupOf(d)] += 1;
            }
            const std::array<float, kGroups> weights{ kWeightTrajPos, kWeightTrajDir, kWeightPastPos, kWeightPastDir,
                                                      kWeightFootPos, kWeightFootVel, kWeightHipVel };
            for (int d = 0; d < F::kDims; ++d) {
                const double spread = groupSpread[GroupOf(d)] / groupDims[GroupOf(d)];
                db->mean[d]   = static_cast<float>(mean[d]);
                db->scale[d]  = static_cast<float>(spread > 1.0e-6 ? spread : 1.0);
                db->weight[d] = weights[GroupOf(d)];
            }
            for (std::size_t r = 0; r < n; ++r)
                for (int d = 0; d < F::kDims; ++d) {
                    float& x = db->rows[r * F::kDims + d];
                    x        = (x - db->mean[d]) / db->scale[d];
                }

            for (std::size_t g = 0; g < gaitSpeeds.size(); ++g) {
                auto& v = gaitSpeeds[g];
                if (v.empty()) continue;
                std::nth_element(v.begin(), v.begin() + v.size() / 2, v.end());
                db->gaitSpeed[g] = v[v.size() / 2];
            }

            const double secs = std::chrono::duration<double>(Clock::now() - t0).count();
            LOG_INFO("motion matching: database built in {:.1f} s - {} of {} clip(s), {} frames x {} features; clip speeds "
                     "walk {:.0f} ({} frames), run {:.0f} ({}), sprint {:.0f} ({}) game units/s",
                     secs, measured, db->clips.size(), n, F::kDims, db->gaitSpeed[1], gaitSpeeds[1].size(), db->gaitSpeed[2],
                     gaitSpeeds[2].size(), db->gaitSpeed[3], gaitSpeeds[3].size());
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

        int FrameOf(const Clip& c, double time)
        {
            return std::clamp(static_cast<int>(std::lround(time / c.dt)), 0, c.frames - 1);
        }

        void CountFrame();

        /// @brief Plays what the search chose: advances it by the frame's time and hands it to the add-on graph.
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

        /// @brief Count a player frame; every kReportEvery seconds, log the clip samples per frame since the last report.
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
            const auto held    = g_held.exchange(0);
            static int reports = 0;
            if (reports == 0)
                LOG_INFO("motion matching: switching - margin {:.0f}%, reselect ban {:.2f} s, forced on {:.0f} units/s or 30 deg",
                         Switching().margin * 100.0f, Switching().reselect, kForceSpeed);
            if (frames > 0 && reports++ < 60)
                LOG_INFO("motion matching: clip samples per frame - add-on clips {:.2f}, every clip {:.1f} (over {} frames); "
                         "{} better candidate(s) held back by the margin",
                         static_cast<double>(ours) / frames, static_cast<double>(all) / frames, frames, held);
        }

    }  // namespace

    bool controller::motion::On()
    {
        static const bool on = core::debug::FlagOn("char_motion_matching");
        return on;
    }

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
        return std::format("clip {} '{}' frame {}, blend left {:.2f} s, last switch {:.2f} s ago", p.clip, c.name,
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

        Playback cur;
        bool     clipEnd = false;
        {
            std::scoped_lock lock(g_shared.lock);
            clipEnd = g_shared.searchNow;
            if (!clipEnd && elapsed < kSearchInterval && !(forced && elapsed >= kForcedSpacing)) return;
            g_shared.searchNow = false;
            cur                = g_shared.havePending ? g_shared.pending : g_shared.now;
        }
        last = now, lastWantX = q.desiredX, lastWantY = q.desiredY;
        const char* trigger = clipEnd ? "clip end" : elapsed < kSearchInterval ? "forced" : "timer";

        /// @note The query: the trajectory ahead and back from the predictor, the pose from the frame playing now.
        std::array<float, F::kDims> qv{};
        for (int k = 0; k < 3; ++k) {
            const float raw[8] = { q.x[k], q.y[k], q.dirX[k], q.dirY[k], q.pastX[k], q.pastY[k], q.pastDirX[k], q.pastDirY[k] };
            const int   at[8]  = { F::kTrajPos + 2 * k, F::kTrajPos + 2 * k + 1, F::kTrajDir + 2 * k, F::kTrajDir + 2 * k + 1,
                                   F::kPastPos + 2 * k, F::kPastPos + 2 * k + 1, F::kPastDir + 2 * k, F::kPastDir + 2 * k + 1 };
            for (int j = 0; j < 8; ++j) qv[at[j]] = (raw[j] - db->mean[at[j]]) / db->scale[at[j]];
        }
        int curRow = -1;
        if (cur.clip >= 0 && db->clips[cur.clip].frames > 0) {
            const Clip& c = db->clips[cur.clip];
            curRow        = c.first + FrameOf(c, cur.time);
            std::copy_n(db->rows.data() + static_cast<std::size_t>(curRow) * F::kDims + F::kFootPos, F::kDims - F::kFootPos,
                        qv.data() + F::kFootPos);
        }

        const auto cost = [&](std::size_t r, float bound) {
            const float* row = db->rows.data() + r * F::kDims;
            float        sum = 0;
            for (int d = 0; d < F::kDims && sum < bound; ++d) {
                const float e = db->weight[d] * (qv[d] - row[d]);
                sum += e * e;
            }
            return sum;
        };

        /// @note The reselect ban: each pick of the last fMMReselectTime, advanced by the clip time played since (at the
        ///       current rate), bans the rows within kIgnoreSurrounding of where it would be now, as merged row ranges.
        struct Pick {
            int               clip  = 0;
            double            frame = 0;
            Clock::time_point at;
        };
        static std::vector<Pick> picks;
        const SwitchTuning&      tune = Switching();
        const float              rate = g_rate.load(std::memory_order_relaxed);
        std::erase_if(picks, [&](const Pick& p) { return std::chrono::duration<double>(now - p.at).count() > tune.reselect; });
        std::vector<std::pair<int, int>> banned;
        for (const Pick& p : picks) {
            const Clip&  c  = db->clips[p.clip];
            const double at = p.frame + std::chrono::duration<double>(now - p.at).count() * rate / c.dt;
            const int    lo = (std::max)(0, static_cast<int>(std::floor(at)) - kIgnoreSurrounding);
            const int    hi = (std::min)(c.frames - 1, static_cast<int>(std::ceil(at)) + kIgnoreSurrounding);
            if (lo <= hi) banned.emplace_back(c.first + lo, c.first + hi);
        }
        std::sort(banned.begin(), banned.end());

        const auto  t0      = Clock::now();
        const float current = curRow >= 0 ? cost(static_cast<std::size_t>(curRow), 1.0e30f) : 1.0e30f;
        float       best    = 1.0e30f;
        int         bestRow = -1;
        std::size_t ban     = 0;
        const std::size_t n = db->clipOf.size();
        /// @note The tag gates: standing (the request near zero) cuts only into idles and stops; moving never into a
        ///       pure idle (a start is tagged Accelerate as well); and a clip marked `entry: changes` only at a change.
        const bool standing = std::hypot(q.desiredX, q.desiredY) < kForceMoving;
        for (std::size_t r = 0; r < n; ++r) {
            if (!db->searchable[r]) continue;
            const std::uint8_t tag = db->rowTags[r];
            if (!(tag & kTagEnter)) continue;
            if (standing ? !(tag & (kTagIdle | kTagDecelerate)) : (tag & kTagIdle) && !(tag & kTagAccelerate)) continue;
            if (curRow >= 0 && db->clipOf[r] == cur.clip && std::abs(static_cast<int>(r) - curRow) < kIgnoreSurrounding) continue;
            while (ban < banned.size() && banned[ban].second < static_cast<int>(r)) ++ban;
            bool skip = false;
            for (std::size_t b = ban; b < banned.size() && banned[b].first <= static_cast<int>(r); ++b)
                if (banned[b].second >= static_cast<int>(r)) { skip = true; break; }
            if (skip) continue;
            const float c = cost(r, best);
            if (c < best) best = c, bestRow = static_cast<int>(r);
        }
        const float searchMs = std::chrono::duration<float, std::milli>(Clock::now() - t0).count();
        CB_PLOT("MM search ms", searchMs);

        /// @note At a clip's end the current frame cannot go on, so the best candidate wins outright; otherwise it must
        ///       beat the current frame by the margin.
        const bool wins = bestRow >= 0 && (curRow < 0 || clipEnd || best < current * (1.0f - tune.margin));
        if (bestRow >= 0 && !wins && best < current) g_held.fetch_add(1, std::memory_order_relaxed);

        static int switches = 0;
        if (wins) {
            const int   clip = db->clipOf[bestRow];
            const Clip& c    = db->clips[clip];
            {
                std::scoped_lock lock(g_shared.lock);
                g_shared.pending     = { clip, (bestRow - c.first) * c.dt };
                g_shared.havePending = true;
            }
            picks.push_back({ clip, static_cast<double>(bestRow - c.first), now });
            ++switches;
            g_lastSwitch.store(now.time_since_epoch().count(), std::memory_order_relaxed);
            CB_PLOT("MM switches", switches);
            CB_MESSAGE("MM -> clip {} '{}' frame {} ({}, cost {:.2f}, was {:.2f})", clip, c.name, bestRow - c.first, trigger, best,
                       current);
            static constexpr const char* kGaitNames[4] = { "none", "walk", "run", "sprint" };
            if (LogBudget())
                LOG_INFO("motion matching: -> clip {} '{}' frame {} x{:.2f} (cost {:.2f}, was {:.2f}; {}; gait {} / frame {}; {} rows in "
                         "{:.2f} ms)",
                         clip, c.name, bestRow - c.first, rate, best, current, trigger,
                         kGaitNames[static_cast<int>(q.gait) & 3], kGaitNames[db->rowGait[bestRow] & 3], n, searchMs);
        } else {
            best    = current;
            bestRow = curRow;
        }

        if (ch::hud::On()) {
            ch::hud::Matching h;
            const int   shown = bestRow >= 0 ? db->clipOf[bestRow] : -1;
            if (shown >= 0) {
                const std::string& name  = db->clips[shown].name;
                const auto         slash = name.find_last_of('\\');
                const std::string  stem  = slash == std::string::npos ? name : name.substr(slash + 1);
                std::snprintf(h.clip, sizeof(h.clip), "%s", stem.c_str());
                h.frame = bestRow - db->clips[shown].first;
            }
            h.cost     = best;
            h.searchMs = searchMs;
            h.switches = switches;
            h.rate     = g_rate.load(std::memory_order_relaxed);
            h.blend    = BlendRemaining();
            ch::hud::Publish(h);
        }
    }

}  // namespace CB::feature
