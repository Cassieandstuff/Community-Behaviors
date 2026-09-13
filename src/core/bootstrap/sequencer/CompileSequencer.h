// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
// ============================================================================
//  CompileSequencer — the phase scheduler (real; SELF-CONTAINED; not yet wired).
// ----------------------------------------------------------------------------
//  This is the machinery only: a table of phases + a driver that walks the DAG.
//  It has NO dependency on the Resolver, SKSE, or anything CB-specific — a phase
//  is just a closure, so the compile bodies capture whatever they need at wire-in.
//  That keeps the ONE genuinely-untestable-without-the-game part (the concurrency)
//  isolated in a unit you can exercise on its own — see SelfTest() in the .cpp.
//
//  It is compiled into the plugin (the source glob picks it up) but nothing calls
//  it, so it is inert until the gate is switched to it behind `sequencer.enable`.
//
//  SHAPE (your TFWR pattern, one extra column): each Phase row pairs a function
//  with the data that says how/when to launch it (`mode`, `dependsOn`); one driver
//  loop walks the table. `dependsOn` is what turns a dispatch table into a
//  scheduler — it lets the loop express ordering + barriers + overlap.
//
//  THE SCHEDULE IT PRODUCES (for our 4-row diamond):
//    skeletons(Serial) ─┬─▶ animations(Parallel) ┐
//                       └─▶ behaviors(Serial)     ┴─▶ adsf(Serial join)
//  animations + behaviors share a readiness wave (both depend only on skeletons),
//  so they OVERLAP; the orchestrator thread waits out each wave, then advances.
//
//  SAFETY of the concurrency: the orchestrator thread (whoever calls Sequencer::run)
//  is the ONLY thread that waits on a barrier; pool workers never call the joining
//  parallel_for, so there is no pool-reentrancy deadlock. Task exceptions are
//  swallowed at the barrier so a failed unit can never hang the wave.
//
//  DETERMINISM (== your byte-gates): each parallel task must write ONLY its own
//  pre-assigned result slot; cross-task assembly is a later Serial phase (adsf),
//  which sorts canonically. Then parallel output == serial output, byte-for-byte —
//  the acceptance test and the race detector in one.
// ============================================================================

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

namespace CB::seq {

enum class Mode { Serial, Parallel };
using PhaseId = std::string;

// One row. Exactly one of {run, tasks} is set, per `mode`. `dependsOn` phases must
// all COMPLETE before this one starts.
struct Phase {
    PhaseId                                                id;
    Mode                                                   mode;
    std::vector<PhaseId>                                   dependsOn;
    std::function<void()>                                  run;    // Serial: the whole phase
    std::function<std::vector<std::function<void()>>()>    tasks;  // Parallel: produce independent tasks
};

// Minimal fixed worker pool. submit() enqueues fire-and-forget; parallel_for() fans a
// batch across the workers and BLOCKS the caller until every job in the batch is done.
class ThreadPool {
public:
    // threads: 0 => hardware_concurrency (min 1). stackBytes: 0 => default thread stack; when non-zero
    // each worker is created with that many bytes of RESERVED stack (Win32 _beginthreadex +
    // STACK_SIZE_PARAM_IS_A_RESERVATION) — the same guard WarmUpThread uses, so a compile task that
    // recurses deeply (Havok graph/animation assembly) can never overflow a default ~1MB worker stack.
    explicit ThreadPool(unsigned threads = 0, std::size_t stackBytes = 0);
    ~ThreadPool();
    ThreadPool(const ThreadPool&)            = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    void     submit(std::function<void()> job);
    void     parallel_for(std::vector<std::function<void()>> jobs);   // run all + JOIN
    unsigned size() const noexcept { return static_cast<unsigned>(workers_.size()); }

private:
    void                     worker();
    static unsigned __stdcall Thunk(void* self);   // Win32 entry -> worker()

    std::vector<void*>                workers_;   // Win32 thread HANDLEs (_beginthreadex)
    std::queue<std::function<void()>> q_;
    std::mutex                        m_;
    std::condition_variable           cv_;
    bool                              stop_ = false;
};

class Sequencer {
public:
    Sequencer& add(Phase p) { phases_.push_back(std::move(p)); return *this; }

    // Wave scheduler: repeatedly run every phase whose deps are satisfied (as one
    // overlapping batch), then advance. Blocks until all phases complete. Throws on a
    // cycle / unmet dependency (no phase ready but work remains).
    void run(ThreadPool& pool);

    const std::vector<Phase>& phases() const noexcept { return phases_; }

private:
    std::vector<Phase> phases_;
};

// Standalone verification of the scheduler — NO game, NO Resolver. Builds the
// skeletons→{animations∥behaviors}→adsf diamond out of instrumented closures and
// asserts the invariants: skeletons complete before animations/behaviors start;
// animations and behaviors overlap; adsf runs last. Returns true on pass. Call it
// from a debug entry point (or a test target) to prove the machinery before wiring.
bool SelfTest();

}  // namespace CB::seq
