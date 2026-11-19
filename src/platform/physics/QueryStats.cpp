#include "platform/physics/QueryStats.h"

#include <algorithm>
#include <format>
#include <mutex>

namespace CB::platform::physics {

    namespace {

        /// @note A function-local static: tags at namespace scope in other TUs register during dynamic init, in any order.
        struct Registry {
            std::mutex               lock;
            std::vector<QueryStats*> all;
        };

        Registry& Tags()
        {
            static Registry registry;
            return registry;
        }

        void RaiseMax(std::atomic<std::uint64_t>& max, std::uint64_t value)
        {
            std::uint64_t seen = max.load(std::memory_order_relaxed);
            while (value > seen && !max.compare_exchange_weak(seen, value, std::memory_order_relaxed)) {}
        }

        bool Over(const QueryBudget& b, std::uint64_t calls, std::uint64_t nanos)
        {
            return (b.callsPerFrame != 0 && calls > b.callsPerFrame) ||
                   (b.msPerFrame > 0.0 && static_cast<double>(nanos) > b.msPerFrame * 1.0e6);
        }

    }  // namespace

    QueryStats::QueryStats(const char* name, QueryBudget budget) : m_name(name ? name : ""), m_budget(budget)
    {
        Registry&             r = Tags();
        const std::lock_guard guard(r.lock);
        r.all.push_back(this);
    }

    QueryStats::~QueryStats()
    {
        Registry&             r = Tags();
        const std::lock_guard guard(r.lock);
        std::erase(r.all, this);
    }

    void QueryStats::Record(std::uint64_t nanos, std::uint64_t lockNanos, bool hit, bool skipped)
    {
        m_calls.fetch_add(1, std::memory_order_relaxed);
        if (hit) m_hits.fetch_add(1, std::memory_order_relaxed);
        if (skipped) m_skipped.fetch_add(1, std::memory_order_relaxed);
        m_nanos.fetch_add(nanos, std::memory_order_relaxed);
        m_lockNanos.fetch_add(lockNanos, std::memory_order_relaxed);
        RaiseMax(m_maxNanos, nanos);
        RaiseMax(m_maxLockNanos, lockNanos);
        m_frameCalls.fetch_add(1, std::memory_order_relaxed);
        m_frameNanos.fetch_add(nanos, std::memory_order_relaxed);
    }

    QueryWindow QueryStats::TakeWindow()
    {
        constexpr auto r = std::memory_order_relaxed;
        QueryWindow    w;
        w.name         = m_name;
        w.calls        = m_calls.exchange(0, r);
        w.hits         = m_hits.exchange(0, r);
        w.nanos        = m_nanos.exchange(0, r);
        w.maxNanos     = m_maxNanos.exchange(0, r);
        w.lockNanos    = m_lockNanos.exchange(0, r);
        w.maxLockNanos = m_maxLockNanos.exchange(0, r);
        w.skipped      = m_skipped.exchange(0, r);
        w.framesOver   = m_framesOver.exchange(0, r);
        return w;
    }

    QueryFrameUse QueryStats::EndFrame()
    {
        QueryFrameUse use;
        use.stats = this;
        use.calls = m_frameCalls.exchange(0, std::memory_order_relaxed);
        use.nanos = m_frameNanos.exchange(0, std::memory_order_relaxed);
        use.over  = Over(m_budget, use.calls, use.nanos);
        if (use.over) {
            m_framesOver.fetch_add(1, std::memory_order_relaxed);
            use.firstOver = !m_warned;
            m_warned      = true;
        }
        return use;
    }

    QueryTimer::~QueryTimer()
    {
        const auto ns = [](QueryClock::duration d) {
            const auto n = std::chrono::duration_cast<std::chrono::nanoseconds>(d).count();
            return static_cast<std::uint64_t>(n > 0 ? n : 0);
        };
        m_stats.Record(ns(QueryClock::now() - m_start), ns(m_lock), m_hit, m_skipped);
    }

    void EndQueryFrame(QueryFrameVisitor visit, void* user)
    {
        Registry&             r = Tags();
        const std::lock_guard guard(r.lock);
        for (QueryStats* tag : r.all) {
            const QueryFrameUse use = tag->EndFrame();
            if (visit) visit(user, use);
        }
    }

    std::vector<QueryWindow> TakeQueryWindows()
    {
        std::vector<QueryWindow> out;
        {
            Registry&             r = Tags();
            const std::lock_guard guard(r.lock);
            out.reserve(r.all.size());
            for (QueryStats* tag : r.all)
                if (QueryWindow w = tag->TakeWindow(); w.calls != 0) out.push_back(w);
        }
        std::ranges::sort(out, [](const QueryWindow& a, const QueryWindow& b) { return a.nanos > b.nanos; });
        return out;
    }

    std::string FormatQueryWindows(std::span<const QueryWindow> windows, std::uint64_t frames)
    {
        if (windows.empty()) return "none";
        const double f = static_cast<double>((std::max)(frames, std::uint64_t{ 1 }));
        std::string  out;
        for (const QueryWindow& w : windows) {
            if (!out.empty()) out += ", ";
            out += std::format("{} {:.1f}x {:.3f} ms (max {:.0f} us, hits {:.0f}%; lock {:.3f} ms, max {:.0f} us", w.name,
                               static_cast<double>(w.calls) / f, static_cast<double>(w.nanos) / f / 1.0e6,
                               static_cast<double>(w.maxNanos) / 1.0e3,
                               100.0 * static_cast<double>(w.hits) / static_cast<double>((std::max)(w.calls, std::uint64_t{ 1 })),
                               static_cast<double>(w.lockNanos) / f / 1.0e6, static_cast<double>(w.maxLockNanos) / 1.0e3);
            if (w.skipped != 0) out += std::format("; skipped {}", w.skipped);
            if (w.framesOver != 0) out += std::format("; over budget {} frames", w.framesOver);
            out += ')';
        }
        return out;
    }

}  // namespace CB::platform::physics
