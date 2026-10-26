#include "PCH.h"

#include "CharComponents.h"

#include <algorithm>
#include <atomic>
#include <vector>

namespace CB::feature::controller::component {

    namespace {

        /// @note A function-local static: components register from their own static initializers, in any order. The
        ///       registry is fixed once the game runs (frames only read it).
        std::vector<Component>& Registry()
        {
            static std::vector<Component> components;
            return components;
        }

        std::atomic<int> g_refusedLogs{ 0 };

    }  // namespace

    bool Register(const Component& c)
    {
        auto& all = Registry();
        all.push_back(c);
        std::stable_sort(all.begin(), all.end(), [](const Component& a, const Component& b) { return a.priority < b.priority; });
        return true;
    }

    void Apply(RE::Actor& actor, Frame& frame)
    {
        for (const Component& c : Registry()) {
            if (!c.active || !c.apply || !c.active(actor)) continue;
            const Support before = frame.support;
            c.apply(actor, frame);
            if (before != Support::Ground && frame.support != before) {
                frame.support = before;
                if (g_refusedLogs.fetch_add(1, std::memory_order_relaxed) < 10)
                    LOG_WARN("components: '{}' claimed the support channel another component holds - refused", c.name);
            }
            if (frame.activeCount < static_cast<int>(frame.active.size())) frame.active[frame.activeCount++] = c.name;
        }
    }

}  // namespace CB::feature::controller::component
