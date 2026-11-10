#include "PCH.h"

#include "CharLoad.h"

#include "CharGround.h"

#include <platform/game/Render.h>
#include <platform/profile/Profile.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>

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

        std::uint64_t Read(LoadCounter c) { return g_counters[static_cast<std::size_t>(c)].load(std::memory_order_relaxed); }

        void Report(Window& w, double seconds)
        {
            const auto delta = [&](LoadCounter c) {
                return static_cast<double>(Read(c) - w.start[static_cast<std::size_t>(c)]);
            };
            const double f = static_cast<double>((std::max)(w.frames, std::uint64_t{ 1 }));
            std::size_t  managed = 0, entries = 0;
            NpcCounts(managed, entries);
            LOG_INFO("load: {:.0f} s, {} frames ({:.1f} ms average, {:.1f} ms worst); per frame: {:.1f} player + {:.1f} NPC predictions "
                     "({:.1f} NPC steps skipped by distance), {:.2f} ms predicting, {:.0f} floor rays, {:.1f} sweeps; NPCs managed {} "
                     "(table {}), predictor states {}",
                     seconds, w.frames, 1000.0 * w.frameSum / f, 1000.0 * w.frameMax, delta(LoadCounter::PredictPlayer) / f,
                     delta(LoadCounter::PredictNpc) / f, delta(LoadCounter::PredictNpcSkipped) / f, delta(LoadCounter::PredictMicros) / f / 1000.0,
                     delta(LoadCounter::Rays) / f, delta(LoadCounter::Sweeps) / f, managed, entries, PredictorStateCount());
        }

        /// @note After the present: the frame time is present to present, the game's whole frame.
        void OnPresent(platform::game::PresentCall::Next next, std::uint32_t arg)
        {
            next(arg);
            Window&    w   = g_window;
            const auto now = Clock::now();
            std::array<std::uint64_t, static_cast<std::size_t>(LoadCounter::kCount)> counts{};
            for (std::size_t i = 0; i < counts.size(); ++i) counts[i] = g_counters[i].load(std::memory_order_relaxed);
            if (w.last != Clock::time_point{}) {
                const double dt = std::chrono::duration<double>(now - w.last).count();
                if (dt < kMaxFrame) {
                    ++w.frames;
                    w.frameSum += dt;
                    w.frameMax = (std::max)(w.frameMax, dt);
                }
                CB_PLOT("CB frame ms", 1000.0 * dt);
                const auto perFrame = [&](LoadCounter c) {
                    const std::size_t i = static_cast<std::size_t>(c);
                    return static_cast<double>(counts[i] - w.lastFrame[i]);
                };
                CB_PLOT("CB floor rays", perFrame(LoadCounter::Rays));
                CB_PLOT("CB sweeps", perFrame(LoadCounter::Sweeps));
                CB_PLOT("CB NPC predictions", perFrame(LoadCounter::PredictNpc));
                CB_PLOT("CB predict ms", perFrame(LoadCounter::PredictMicros) / 1000.0);
            } else {
                w.opened = now;
                w.start  = counts;
            }
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

}  // namespace CB::feature::controller
