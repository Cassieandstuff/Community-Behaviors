// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/bootstrap/sequencer/CompileSequencer.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <unordered_set>

#ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#    define NOMINMAX
#endif
#include <process.h>    // _beginthreadex
#include <windows.h>    // WaitForSingleObject / CloseHandle / STACK_SIZE_PARAM_IS_A_RESERVATION

namespace CB::seq {

// ── ThreadPool ──────────────────────────────────────────────────────────────
unsigned __stdcall ThreadPool::Thunk(void* self) {
    static_cast<ThreadPool*>(self)->worker();
    return 0;
}

ThreadPool::ThreadPool(unsigned threads, std::size_t stackBytes) {
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

// ── Sequencer (wave scheduler) ──────────────────────────────────────────────
void Sequencer::run(ThreadPool& pool) {
    std::unordered_set<PhaseId> done;
    while (done.size() < phases_.size()) {
        // This wave = every not-done phase whose deps are ALL done.
        std::vector<const Phase*> wave;
        for (const Phase& p : phases_) {
            if (done.count(p.id)) continue;
            const bool ready = std::all_of(p.dependsOn.begin(), p.dependsOn.end(),
                                           [&](const PhaseId& d) { return done.count(d) != 0; });
            if (ready) wave.push_back(&p);
        }
        if (wave.empty())
            throw std::runtime_error("CompileSequencer: unmet dependency or cycle — no phase ready");

        // Flatten the whole wave into ONE batch so independent phases overlap.
        // SERIAL phase jobs go FIRST: a serial phase (e.g. behaviors) must grab a worker
        // immediately and run ALONGSIDE the parallel tasks, not queue behind all of them —
        // otherwise on an N-worker pool a lone serial job lands last and runs AFTER the
        // whole parallel set, defeating the overlap (behaviors hiding behind animations).
        std::vector<std::function<void()>> batch;
        for (const Phase* p : wave)
            if (p->mode == Mode::Serial && p->run) batch.push_back(p->run);
        for (const Phase* p : wave)
            if (p->mode == Mode::Parallel && p->tasks) {
                std::vector<std::function<void()>> ts = p->tasks();   // produced on the orchestrator
                for (auto& t : ts) batch.push_back(std::move(t));
            }
        pool.parallel_for(std::move(batch));    // orchestrator blocks until the wave finishes
        for (const Phase* p : wave) done.insert(p->id);
    }
}

// ── SelfTest (no game, no Resolver) ─────────────────────────────────────────
bool SelfTest() {
    using namespace std::chrono_literals;

    std::atomic<bool> skelDone{ false };
    std::atomic<bool> behActive{ false };
    std::atomic<int>  animOverlapObservations{ 0 };
    std::atomic<int>  animTasksRun{ 0 };
    std::atomic<bool> barrierViolation{ false };

    ThreadPool pool(4);
    Sequencer  seq;

    seq.add(Phase{ "skeletons", Mode::Serial, /*deps*/ {},
        /*run*/ [&] { std::this_thread::sleep_for(10ms); skelDone.store(true); },
        /*tasks*/ nullptr });

    seq.add(Phase{ "animations", Mode::Parallel, /*deps*/ { "skeletons" },
        /*run*/ nullptr,
        /*tasks*/ [&]() -> std::vector<std::function<void()>> {
            if (!skelDone.load()) barrierViolation.store(true);       // produced too early?
            std::vector<std::function<void()>> ts;
            for (int i = 0; i < 8; ++i)
                ts.push_back([&] {
                    if (!skelDone.load()) barrierViolation.store(true);   // ran before its dep?
                    // Sample across the task's lifetime (not just at start): behaviors is the
                    // last job in the batch, so an early task would miss a start-only check.
                    for (int k = 0; k < 20; ++k) {
                        if (behActive.load()) animOverlapObservations.fetch_add(1);
                        std::this_thread::sleep_for(1ms);
                    }
                    animTasksRun.fetch_add(1);
                });
            return ts;
        } });

    seq.add(Phase{ "behaviors", Mode::Serial, /*deps*/ { "skeletons" },   // NOT deps animations → overlaps
        /*run*/ [&] {
            if (!skelDone.load()) barrierViolation.store(true);
            behActive.store(true);
            std::this_thread::sleep_for(40ms);   // generous window so overlap is deterministic
            behActive.store(false);
        },
        /*tasks*/ nullptr });

    seq.add(Phase{ "adsf", Mode::Serial, /*deps*/ { "animations", "behaviors" },
        /*run*/ [&] {
            if (!skelDone.load() || behActive.load() || animTasksRun.load() != 8)
                barrierViolation.store(true);   // join must see everything upstream complete
        },
        /*tasks*/ nullptr });

    seq.run(pool);

    // PASS iff: no barrier was crossed early, all 8 animation tasks ran, and at least
    // one animation task observed behaviors active at the same time (proves overlap).
    return !barrierViolation.load()
        && animTasksRun.load() == 8
        && animOverlapObservations.load() > 0;
}

}  // namespace CB::seq
