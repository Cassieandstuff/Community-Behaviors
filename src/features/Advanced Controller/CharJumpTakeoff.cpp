#include "PCH.h"

#include "CharJumpTakeoff.h"

#include <atomic>

namespace CB::feature::controller::takeoff {

    namespace {
        std::atomic<std::uint32_t> g_counter{ 0 };
        std::atomic<std::uint32_t> g_pending{ 0 };   ///< the pending wind-up's generation, 0 = none
        std::atomic<std::uint32_t> g_ready{ 0 };     ///< the generation the node declared ready
    }  // namespace

    std::uint32_t Begin()
    {
        std::uint32_t gen = g_counter.fetch_add(1, std::memory_order_relaxed) + 1;
        if (gen == 0) gen = g_counter.fetch_add(1, std::memory_order_relaxed) + 1;
        g_ready.store(0, std::memory_order_relaxed);
        g_pending.store(gen, std::memory_order_release);
        return gen;
    }

    void End() { g_pending.store(0, std::memory_order_release); }

    bool Pending() { return g_pending.load(std::memory_order_acquire) != 0; }

    std::uint32_t Generation() { return g_pending.load(std::memory_order_acquire); }

    void Ready(std::uint32_t generation)
    {
        if (generation != 0 && generation == g_pending.load(std::memory_order_acquire)) g_ready.store(generation, std::memory_order_release);
    }

    bool IsReady()
    {
        const std::uint32_t pending = g_pending.load(std::memory_order_acquire);
        return pending != 0 && g_ready.load(std::memory_order_acquire) == pending;
    }

}  // namespace CB::feature::controller::takeoff
