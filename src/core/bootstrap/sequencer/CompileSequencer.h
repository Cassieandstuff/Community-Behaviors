// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace CB::core::bootstrap {

// Minimal fixed worker pool. submit() enqueues fire-and-forget; parallel_for() fans a
// batch across the workers and BLOCKS the caller until every job in the batch is done.
class ThreadPool {
public:
    // threads: 0 => hardware_concurrency (min 1). stackBytes: 0 => default thread stack; when non-zero
    // each worker is created with that many bytes of RESERVED stack (Win32 _beginthreadex +
    // STACK_SIZE_PARAM_IS_A_RESERVATION) — the same guard WarmUpThread uses, so a compile task that
    // recurses deeply (Havok graph/animation assembly) can never overflow a default ~1MB worker stack.
    // threadPriority: a Win32 THREAD_PRIORITY_* value applied to each worker (e.g.
    // THREAD_PRIORITY_BELOW_NORMAL). 0 (THREAD_PRIORITY_NORMAL) => leave the default. Used to keep a
    // heavy background compile from starving the game's render/main threads (the load-minimize).
    explicit ThreadPool(unsigned threads = 0, std::size_t stackBytes = 0, int threadPriority = 0);
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
    int                               priority_ = 0;   // Win32 THREAD_PRIORITY_* applied per worker (0 = default)
};

}  // namespace CB::core::bootstrap
