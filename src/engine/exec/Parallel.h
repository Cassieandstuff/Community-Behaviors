#pragma once
/// @file
/// @brief CB::engine::exec — index-parallel loops over short-lived std::thread workers.
/// @note For offline tools (converter, hky-tool) and engine codecs. Inside the game process, prefer the plugin's
///       BELOW_NORMAL ThreadPool (an executor injected by the caller) so a loop never out-competes the game's threads.

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <exception>
#include <functional>
#include <mutex>
#include <system_error>
#include <thread>
#include <type_traits>
#include <vector>

namespace CB::engine::exec {

/// @brief A caller-owned executor: runs every task (possibly concurrently) and returns once all have finished — the
///        plugin's BELOW_NORMAL ThreadPool::parallel_for contract.
/// @warning Must not be invoked from one of its own tasks (a nested wait on the same pool can deadlock it).
using TaskRunner = std::function<void(std::vector<std::function<void()>>)>;

struct ParallelOptions {
    unsigned                 threads = 0;         ///< worker count; 0 = hardware_concurrency
    const std::atomic<bool>* cancel  = nullptr;   ///< once true, no further index is handed out
    /// @brief When set, the workers run as tasks on this executor instead of on fresh std::threads — how code inside
    ///        the game process keeps a loop on the plugin's BELOW_NORMAL pool.
    const TaskRunner*        runner  = nullptr;
};

/// @brief The number of workers ParallelFor(n, …, opt) runs: opt.threads (0 = hardware_concurrency), at least 1, at
///        most n. Size per-worker scratch with it.
inline unsigned WorkerCount(std::size_t n, const ParallelOptions& opt = {})
{
    unsigned t = opt.threads ? opt.threads : std::thread::hardware_concurrency();
    if (t == 0) t = 1;
    if (n < t) t = static_cast<unsigned>(std::max<std::size_t>(n, 1));
    return t;
}

/// @brief Run `f(i)` — or `f(i, worker)` when `f` takes a worker index — for every i in [0, n), and return once all
///        of them have finished.
/// @param f Called concurrently: it may touch only its own index's state (per-index output slots keep results
///        deterministic and independent of thread count) or read-only shared state. `worker` is in
///        [0, WorkerCount(n, opt)) and is stable for the calling thread — key per-worker scratch by it.
/// @note Worker 0 is the calling thread. A failure to start a thread is not an error: the started workers drain the
///       remaining indices.
/// @post The first exception `f` throws stops the handing-out of indices and is rethrown here after every worker has
///       joined (a bare std::thread would std::terminate). On cancel, indices not yet handed out are skipped silently
///       — the caller checks its flag.
template <class F>
void ParallelFor(std::size_t n, F&& f, ParallelOptions opt = {})
{
    if (n == 0) return;
    const unsigned           nWorkers = WorkerCount(n, opt);
    std::atomic<std::size_t> next{ 0 };
    std::atomic<bool>        failed{ false };
    std::exception_ptr       first;
    std::mutex               firstMx;

    const auto run = [&](unsigned worker) {
        try {
            for (;;) {
                if (failed.load(std::memory_order_relaxed)) return;
                if (opt.cancel && opt.cancel->load(std::memory_order_relaxed)) return;
                const std::size_t i = next.fetch_add(1, std::memory_order_relaxed);
                if (i >= n) return;
                if constexpr (std::is_invocable_v<F&, std::size_t, unsigned>) f(i, worker);
                else f(i);
            }
        } catch (...) {
            std::lock_guard<std::mutex> lk(firstMx);
            if (!first) first = std::current_exception();
            failed.store(true, std::memory_order_relaxed);
        }
    };

    if (opt.runner && *opt.runner) {
        std::vector<std::function<void()>> tasks;
        tasks.reserve(nWorkers);
        for (unsigned w = 0; w < nWorkers; ++w) tasks.emplace_back([&run, w] { run(w); });
        (*opt.runner)(std::move(tasks));
        if (first) std::rethrow_exception(first);
        return;
    }

    std::vector<std::thread> pool;
    pool.reserve(nWorkers - 1);
    for (unsigned w = 1; w < nWorkers; ++w) {
        try { pool.emplace_back(run, w); }
        catch (const std::system_error&) { break; }
    }
    run(0);
    for (auto& t : pool) t.join();
    if (first) std::rethrow_exception(first);
}

}  // namespace CB::engine::exec
