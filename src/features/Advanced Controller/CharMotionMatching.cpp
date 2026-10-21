#include "PCH.h"

#include "CharDebugHud.h"
#include "CharMotionMatching.h"

#include <core/debug/Settings.h>
#include <core/resolve/LoadOrderReader.h>
#include <codec/format/AnimationYamlLoader.h>
#include <compile/MotionFeatures.h>
#include <platform/game/Movement.h>
#include <platform/hooks/Override.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
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
        constexpr int    kIgnoreSurrounding = 20;    ///< frames: a candidate this close in the current clip is the current
        constexpr double kEndMargin         = 0.1;   ///< seconds before a clip's end that force a search

        /// @brief Each feature group's weight (Holden's demo defaults).
        /// @note  Applied at search time, never stored in the database, so a weight change needs no rebuild.
        constexpr float kWeightTrajPos = 1.0f, kWeightTrajDir = 1.5f, kWeightFootPos = 0.75f, kWeightFootVel = 1.0f,
                        kWeightHipVel = 1.0f;

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
            std::array<float, F::kDims>     mean{}, scale{}, weight{};
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
        std::atomic<int>   g_logs{ 0 };
        bool LogBudget() { return g_logs.fetch_add(1, std::memory_order_relaxed) < 400; }

        int GroupOf(int d)
        {
            if (d < F::kTrajDir) return 0;
            if (d < F::kFootPos) return 1;
            if (d < F::kFootVel) return 2;
            if (d < F::kHipVel) return 3;
            return 4;
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
            std::array<double, 5> groupSpread{}, groupDims{};
            for (int d = 0; d < F::kDims; ++d) {
                groupSpread[GroupOf(d)] += std::sqrt(var[d] / static_cast<double>(n));
                groupDims[GroupOf(d)] += 1;
            }
            const std::array<float, 5> weights{ kWeightTrajPos, kWeightTrajDir, kWeightFootPos, kWeightFootVel, kWeightHipVel };
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

            const double secs = std::chrono::duration<double>(Clock::now() - t0).count();
            LOG_INFO("motion matching: database built in {:.1f} s - {} of {} clip(s), {} frames x {} features",
                     secs, measured, db->clips.size(), n, F::kDims);
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

        /// @brief Plays what the search chose: advances it by the frame's time and hands it to the add-on graph.
        void OnFrame(Frame::Next next, RE::MovementControllerNPC* controller, float deltaTime, game::MovementFrame* out)
        {
            next(controller, deltaTime, out);
            RE::Actor*       player = out ? PlayerOf(controller) : nullptr;
            const Database*  db     = g_db.load(std::memory_order_acquire);
            if (!player || !db) return;

            Playback p;
            {
                std::scoped_lock lock(g_shared.lock);
                Playback& now = g_shared.now;
                if (g_shared.havePending) {
                    now                  = g_shared.pending;
                    g_shared.havePending = false;
                } else if (now.clip >= 0) {
                    now.time += deltaTime;
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

    }  // namespace

    bool controller::motion::On()
    {
        static const bool on = core::debug::FlagOn("char_motion_matching");
        return on;
    }

    void controller::motion::Search(const Query& q)
    {
        const Database* db = g_db.load(std::memory_order_acquire);
        if (!db) return;
        static Clock::time_point last{};
        const auto               now = Clock::now();
        Playback                 cur;
        {
            std::scoped_lock lock(g_shared.lock);
            if (!g_shared.searchNow && std::chrono::duration<double>(now - last).count() < kSearchInterval) return;
            g_shared.searchNow = false;
            cur                = g_shared.havePending ? g_shared.pending : g_shared.now;
        }
        last = now;

        /// @note The query: the trajectory from the predictor, the pose from the frame playing now (Holden's).
        std::array<float, F::kDims> qv{};
        for (int k = 0; k < 3; ++k) {
            const float raw[4] = { q.x[k], q.y[k], q.dirX[k], q.dirY[k] };
            const int   at[4]  = { F::kTrajPos + 2 * k, F::kTrajPos + 2 * k + 1, F::kTrajDir + 2 * k, F::kTrajDir + 2 * k + 1 };
            for (int j = 0; j < 4; ++j) qv[at[j]] = (raw[j] - db->mean[at[j]]) / db->scale[at[j]];
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

        const auto  t0      = Clock::now();
        float       best    = curRow >= 0 ? cost(static_cast<std::size_t>(curRow), 1.0e30f) : 1.0e30f;
        const float current = best;
        int         bestRow = curRow;
        const std::size_t n = db->clipOf.size();
        for (std::size_t r = 0; r < n; ++r) {
            if (!db->searchable[r]) continue;
            if (curRow >= 0 && db->clipOf[r] == cur.clip && std::abs(static_cast<int>(r) - curRow) < kIgnoreSurrounding) continue;
            const float c = cost(r, best);
            if (c < best) best = c, bestRow = static_cast<int>(r);
        }
        const float searchMs = std::chrono::duration<float, std::milli>(Clock::now() - t0).count();

        static int switches = 0;
        if (bestRow >= 0 && bestRow != curRow) {
            const int   clip = db->clipOf[bestRow];
            const Clip& c    = db->clips[clip];
            {
                std::scoped_lock lock(g_shared.lock);
                g_shared.pending     = { clip, (bestRow - c.first) * c.dt };
                g_shared.havePending = true;
            }
            ++switches;
            if (LogBudget())
                LOG_INFO("motion matching: -> clip {} '{}' frame {} (cost {:.2f}, was {:.2f}; {} rows in {:.2f} ms)", clip, c.name,
                         bestRow - c.first, best, current, n, searchMs);
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
            ch::hud::Publish(h);
        }
    }

}  // namespace CB::feature
