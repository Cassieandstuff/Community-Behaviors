// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/bootstrap/sequencer/CompileSequencer.h"

#include <atomic>
#include <cstdint>

#ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#    define NOMINMAX
#endif
#include <process.h>    // _beginthreadex
#include <windows.h>    // WaitForSingleObject / CloseHandle / STACK_SIZE_PARAM_IS_A_RESERVATION

namespace CB::core::bootstrap {

// ── ThreadPool ──────────────────────────────────────────────────────────────
unsigned __stdcall ThreadPool::Thunk(void* self) {
    auto* pool = static_cast<ThreadPool*>(self);
    // Drop to the requested priority BEFORE running any work, so the whole compile stays below the
    // game's render/main threads and can't starve them (the load-minimize). 0 == leave the default.
    if (pool->priority_ != 0)
        SetThreadPriority(GetCurrentThread(), pool->priority_);
    pool->worker();
    return 0;
}

ThreadPool::ThreadPool(unsigned threads, std::size_t stackBytes, int threadPriority)
    : priority_(threadPriority) {
    unsigned n = threads ? threads : std::thread::hardware_concurrency();
    if (n == 0) n = 1;
    // STACK_SIZE_PARAM_IS_A_RESERVATION only takes effect when a size is given; with size 0 the flag is
    // meaningless, so pass it only for a non-default stack. A reservation is address space, not committed
    // memory, so a large per-worker reserve is cheap on x64.
    const unsigned initFlag = stackBytes ? STACK_SIZE_PARAM_IS_A_RESERVATION : 0u;
    workers_.reserve(n);
    for (unsigned i = 0; i < n; ++i) {
        const std::uintptr_t h = _beginthreadex(nullptr, static_cast<unsigned>(stackBytes),
                                                &ThreadPool::Thunk, this, initFlag, nullptr);
        if (h) workers_.push_back(reinterpret_cast<void*>(h));
    }
    if (workers_.empty()) {   // never leave a pool with zero workers (parallel_for would hang)
        const std::uintptr_t h = _beginthreadex(nullptr, 0, &ThreadPool::Thunk, this, 0u, nullptr);
        if (h) workers_.push_back(reinterpret_cast<void*>(h));
    }
}

ThreadPool::~ThreadPool() {
    { std::lock_guard<std::mutex> lk(m_); stop_ = true; }
    cv_.notify_all();
    for (void* h : workers_)
        if (h) { WaitForSingleObject(reinterpret_cast<HANDLE>(h), INFINITE); CloseHandle(reinterpret_cast<HANDLE>(h)); }
}

void ThreadPool::worker() {
    for (;;) {
        std::function<void()> job;
        {
            std::unique_lock<std::mutex> lk(m_);
            cv_.wait(lk, [this] { return stop_ || !q_.empty(); });
            if (stop_ && q_.empty()) return;
            job = std::move(q_.front());
            q_.pop();
        }
        job();   // jobs are wrapped for exception-safety by parallel_for
    }
}

void ThreadPool::submit(std::function<void()> job) {
    { std::lock_guard<std::mutex> lk(m_); q_.push(std::move(job)); }
    cv_.notify_one();
}

void ThreadPool::parallel_for(std::vector<std::function<void()>> jobs) {
    if (jobs.empty()) return;
    std::atomic<std::size_t> remaining{ jobs.size() };
    std::mutex               dm;
    std::condition_variable  dcv;
    for (auto& j : jobs) {
        submit([job = std::move(j), &remaining, &dm, &dcv]() mutable {
            // Swallow: a failed unit must NEVER leave the barrier un-decremented (that
            // would hang the orchestrator). Correctness of a compile failure is the
            // phase's own concern (it reports via its result slot), not the scheduler's.
            try { if (job) job(); } catch (...) {}
            if (remaining.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                std::lock_guard<std::mutex> lk(dm);   // publish before notify (no lost wakeup)
                dcv.notify_all();
            }
        });
    }
    std::unique_lock<std::mutex> lk(dm);
    dcv.wait(lk, [&] { return remaining.load(std::memory_order_acquire) == 0; });
    // `remaining`, `dm`, `dcv` are locals but outlive every job: we only return once
    // remaining == 0, i.e. after the last job has finished touching them.
}

}  // namespace CB::core::bootstrap
