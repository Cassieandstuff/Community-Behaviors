#include "PCH.h"

#include "core/bootstrap/ProgressOverlay.h"

#include <atomic>

namespace CB::core::bootstrap {

    namespace {
        std::atomic<std::size_t> s_done{ 0 };
        std::atomic<std::size_t> s_total{ 0 };
        std::atomic<bool>        s_running{ false };
        std::atomic<const char*> s_phase{ "compiling behaviors" };
    }

    void SetProgress(std::size_t done, std::size_t total, bool running)
    {
        s_done.store(done, std::memory_order_relaxed);
        s_total.store(total, std::memory_order_relaxed);
        s_running.store(running, std::memory_order_release);
    }

    bool ReadProgress(std::size_t& done, std::size_t& total)
    {
        const bool running = s_running.load(std::memory_order_acquire);
        done  = s_done.load(std::memory_order_relaxed);
        total = s_total.load(std::memory_order_relaxed);
        return running;
    }

    void SetPhase(const char* phase) { s_phase.store(phase, std::memory_order_release); }

    const char* ReadPhase() { return s_phase.load(std::memory_order_acquire); }

}  // namespace CB::core::bootstrap
