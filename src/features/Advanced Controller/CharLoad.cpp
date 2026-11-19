#include "PCH.h"

#include "CharLoad.h"

#include "CharGround.h"

#include <platform/game/Render.h>
#include <platform/hooks/Override.h>
#include <platform/physics/QueryStats.h>
#include <platform/profile/Profile.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include <Behavior/Behavior/Generator/Clip/hkbClipGenerator.h>

namespace CB::feature::controller {

    namespace {

        using Clock     = std::chrono::steady_clock;

        constexpr double kReportEvery = 30.0;   ///< seconds between log lines
        constexpr double kMaxFrame    = 5.0;    ///< seconds; a longer gap (a load screen, the game minimized) is no frame

        std::array<std::atomic<std::uint64_t>, static_cast<std::size_t>(LoadCounter::kCount)> g_counters{};

        /// @note The present thread's alone.
        struct Window {
            std::array<std::uint64_t, static_cast<std::size_t>(LoadCounter::kCount)> lastFrame{}, start{};
            Clock::time_point                                                    last{}, opened{};
            std::uint64_t                                                        frames = 0;
            double                                                               frameSum = 0, frameMax = 0;
        };
        Window g_window;

        std::mutex  g_nonFiniteLock;
        std::string g_nonFinite;   ///< the last non-finite input pose since the last report

        std::uint64_t Read(LoadCounter c) { return g_counters[static_cast<std::size_t>(c)].load(std::memory_order_relaxed); }

        void Report(Window& w, double seconds)
        {
            const auto delta = [&](LoadCounter c) {
                return static_cast<double>(Read(c) - w.start[static_cast<std::size_t>(c)]);
            };
            const double f = static_cast<double>((std::max)(w.frames, std::uint64_t{ 1 }));
            std::size_t  managed = 0, entries = 0;
            NpcCounts(managed, entries);
            const std::vector<platform::physics::QueryWindow> queries = platform::physics::TakeQueryWindows();
            std::uint64_t                                     calls = 0, nanos = 0;
            for (const platform::physics::QueryWindow& q : queries) calls += q.calls, nanos += q.nanos;
            LOG_INFO("load: {:.0f} s, {} frames ({:.1f} ms average, {:.1f} ms worst); per frame: {:.1f} player + {:.1f} NPC predictions "
                     "({:.1f} NPC steps skipped by distance), {:.2f} ms predicting, {:.1f} physics queries in {:.2f} ms; NPCs managed {} "
                     "(table {}), predictor states {}",
                     seconds, w.frames, 1000.0 * w.frameSum / f, 1000.0 * w.frameMax, delta(LoadCounter::PredictPlayer) / f,
                     delta(LoadCounter::PredictNpc) / f, delta(LoadCounter::PredictNpcSkipped) / f, delta(LoadCounter::PredictMicros) / f / 1000.0,
                     static_cast<double>(calls) / f, static_cast<double>(nanos) / f / 1.0e6, managed, entries, PredictorStateCount());
            LOG_INFO("load: queries per frame by tag: {}", platform::physics::FormatQueryWindows(queries, w.frames));
            std::string nonFinite;
            {
                std::scoped_lock guard(g_nonFiniteLock);
                nonFinite = std::exchange(g_nonFinite, std::string{});
            }
            const double samples = delta(LoadCounter::ClipGenerates);
            LOG_INFO("load: clips per frame: {:.1f} samples in {:.2f} ms ({:.2f} us each); pose search: {:.0f} node activations, {:.0f} "
                     "deactivations, {:.0f} outputs from a non-finite input pose{}{}",
                     samples / f, delta(LoadCounter::ClipGenerateMicros) / f / 1000.0,
                     samples > 0 ? delta(LoadCounter::ClipGenerateMicros) / samples : 0.0, delta(LoadCounter::PoseSearchActivates),
                     delta(LoadCounter::PoseSearchDeactivates), delta(LoadCounter::PoseNonFinite), nonFinite.empty() ? "" : " - last ",
                     nonFinite);
        }

        /// @brief Times every clip's sample, every character's: the cost of a pose-search node's active children shows on
        ///        the report whether or not motion matching runs.
        using ClipGenerate = platform::hooks::Override<&::hkbClipGenerator::generate>;

        void OnClipGenerate(ClipGenerate::Next next, ::hkbClipGenerator* clip, const ::hkbContext& context,
                            const ::hkbGeneratorOutput** activeChildrenOutput, ::hkbGeneratorOutput& output, ::hkReal timeOffset)
        {
            const auto start = Clock::now();
            next(clip, context, activeChildrenOutput, output, timeOffset);
            const auto us = std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - start).count();
            CountLoad(LoadCounter::ClipGenerates);
            CountLoad(LoadCounter::ClipGenerateMicros, static_cast<std::uint64_t>(us > 0 ? us : 0));
        }

        [[maybe_unused]] const bool s_clipTimed =
            ClipGenerate::Subscribe(&OnClipGenerate, platform::hooks::Priority::Normal, "CharLoad", "char_load_report");

        /// @brief Each tag's frame: plotted to Tracy (milliseconds, under the tag's name), and logged the first time it goes
        ///        over its budget.
        void OnQueryFrame(void*, const platform::physics::QueryFrameUse& use)
        {
            const char* name = use.stats->Name();
            CB_PLOT(name, static_cast<double>(use.nanos) / 1.0e6);
            if (!use.firstOver) return;
            const platform::physics::QueryBudget& b = use.stats->Budget();
            LOG_WARN("load: physics queries '{}' over budget: {} calls in {:.3f} ms this frame (budget {} calls, {:.3f} ms; 0 = none); "
                     "logged once, the load report counts the frames over",
                     name, use.calls, static_cast<double>(use.nanos) / 1.0e6, b.callsPerFrame, b.msPerFrame);
        }

        /// @note After the present: the frame time is present to present, the game's whole frame.
        void OnPresent(platform::game::PresentCall::Next next, std::uint32_t arg)
        {
            next(arg);
            Window&    w   = g_window;
            const auto now = Clock::now();
            std::array<std::uint64_t, static_cast<std::size_t>(LoadCounter::kCount)> counts{};
            for (std::size_t i = 0; i < counts.size(); ++i) counts[i] = g_counters[i].load(std::memory_order_relaxed);
            bool frame = false;
            if (w.last != Clock::time_point{}) {
                const double dt = std::chrono::duration<double>(now - w.last).count();
                frame           = dt < kMaxFrame;
                if (frame) {
                    ++w.frames;
                    w.frameSum += dt;
                    w.frameMax = (std::max)(w.frameMax, dt);
                }
                CB_PLOT("CB frame ms", 1000.0 * dt);
                const auto perFrame = [&](LoadCounter c) {
                    const std::size_t i = static_cast<std::size_t>(c);
                    return static_cast<double>(counts[i] - w.lastFrame[i]);
                };
                CB_PLOT("CB NPC predictions", perFrame(LoadCounter::PredictNpc));
                CB_PLOT("CB predict ms", perFrame(LoadCounter::PredictMicros) / 1000.0);
            } else {
                w.opened = now;
                w.start  = counts;
                (void)platform::physics::TakeQueryWindows();
            }
            /// @note A gap that is no frame (the first present, a load screen) closes the tags' frame unchecked: what was
            ///       cast during a load is no frame's budget. The first window's queries likewise start here, not at load.
            platform::physics::EndQueryFrame(frame ? &OnQueryFrame : nullptr, nullptr);
            w.last      = now;
            w.lastFrame = counts;
            const double open = std::chrono::duration<double>(now - w.opened).count();
            if (open < kReportEvery) return;
            Report(w, open);
            w.opened   = now;
            w.start    = counts;
            w.frames   = 0;
            w.frameSum = w.frameMax = 0;
        }

        [[maybe_unused]] const bool s_subscribed =
            platform::game::PresentCall::Subscribe(&OnPresent, platform::hooks::Priority::Normal, "CharLoad", "char_load_report");

    }  // namespace

    void CountLoad(LoadCounter counter, std::uint64_t n)
    {
        g_counters[static_cast<std::size_t>(counter)].fetch_add(n, std::memory_order_relaxed);
    }

    void NotePoseNonFinite(std::string what)
    {
        std::scoped_lock guard(g_nonFiniteLock);
        g_nonFinite = std::move(what);
    }

}  // namespace CB::feature::controller
