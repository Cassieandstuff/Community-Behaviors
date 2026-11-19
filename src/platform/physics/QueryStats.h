#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

/// @file
/// @brief What CB's physics queries cost, per caller tag: calls, hits, time, the wait for the world lock, and a per-frame
///        budget. Every query goes through a backend of hky::physics::IPhysicsWorld (platform::physics::GameWorld in
///        game), which times itself into the tag it was made with; this file is the backend-free half, so it is tested
///        without the game.
/// @note  Why: a session's frame rate fell to 12-25 fps for minutes while the controller's own counters stayed flat, and
///        nothing said which caller cast what, or whether a cast waited on the world. A tag answers both, in the 30 s
///        `load:` report and in Tracy.

namespace CB::platform::physics {

/// @brief The clock queries are timed by.
/// @note  MSVC's steady_clock reads QueryPerformanceCounter.
using QueryClock = std::chrono::steady_clock;

/// @brief A tag's per-frame limit. 0 is no limit.
struct QueryBudget {
    std::uint32_t callsPerFrame = 0;
    double        msPerFrame    = 0.0;
};

/// @brief A tag's use over one report window (TakeQueryWindows), times in nanoseconds.
struct QueryWindow {
    const char*   name         = "";
    std::uint64_t calls        = 0;
    std::uint64_t hits         = 0;
    std::uint64_t nanos        = 0;   ///< the whole query, lock wait included
    std::uint64_t maxNanos     = 0;
    std::uint64_t lockNanos    = 0;   ///< waiting for the world lock
    std::uint64_t maxLockNanos = 0;
    std::uint64_t skipped      = 0;   ///< queries the backend refused to run, reported as no hit (the game's pick budget)
    std::uint64_t framesOver   = 0;   ///< frames over the tag's budget
};

class QueryStats;

/// @brief One tag's use in the frame EndQueryFrame closes.
struct QueryFrameUse {
    const QueryStats* stats     = nullptr;
    std::uint64_t     calls     = 0;
    std::uint64_t     nanos     = 0;
    bool              over      = false;   ///< over the tag's budget this frame
    bool              firstOver = false;   ///< ...and never before: the one frame worth a log line
};

/// @brief One caller's query counters, registered for the report while it lives.
/// @pre   `name` outlives the stats (a string literal): reports and Tracy plots keep the pointer.
/// @note  Tags are made once, at namespace scope (or function-local statics), and passed to the backend per query.
class QueryStats {
public:
    explicit QueryStats(const char* name, QueryBudget budget = {});
    ~QueryStats();
    QueryStats(const QueryStats&)            = delete;
    QueryStats& operator=(const QueryStats&) = delete;

    [[nodiscard]] const char*        Name() const { return m_name; }
    [[nodiscard]] const QueryBudget& Budget() const { return m_budget; }

    /// @brief Counts one query. @note Any thread; relaxed atomics only.
    /// @param skipped the backend did not run it and reports no hit (QueryWindow::skipped).
    void Record(std::uint64_t nanos, std::uint64_t lockNanos, bool hit, bool skipped = false);

    /// @return the use since the last call, which it resets.
    /// @pre One thread at a time (the report's).
    QueryWindow TakeWindow();

    /// @brief Closes this tag's frame: its frame counters are read and reset and held to the budget.
    /// @pre One thread at a time (the frame's end).
    QueryFrameUse EndFrame();

private:
    const char*                m_name;
    QueryBudget                m_budget;
    std::atomic<std::uint64_t> m_calls{ 0 }, m_hits{ 0 }, m_nanos{ 0 }, m_maxNanos{ 0 }, m_lockNanos{ 0 }, m_maxLockNanos{ 0 };
    std::atomic<std::uint64_t> m_skipped{ 0 }, m_framesOver{ 0 };
    std::atomic<std::uint64_t> m_frameCalls{ 0 }, m_frameNanos{ 0 };
    bool                       m_warned = false;
};

/// @brief Times one query into a tag, from construction to destruction.
/// @note  The backend reports the world-lock wait it measured (LockWaited) and whether the query hit.
class QueryTimer {
public:
    explicit QueryTimer(QueryStats& stats) : m_stats(stats), m_start(QueryClock::now()) {}
    ~QueryTimer();
    QueryTimer(const QueryTimer&)            = delete;
    QueryTimer& operator=(const QueryTimer&) = delete;

    void LockWaited(QueryClock::duration wait) { m_lock += wait; }
    void Hit(bool hit) { m_hit = hit; }
    void Skipped() { m_skipped = true; }

private:
    QueryStats&           m_stats;
    QueryClock::time_point m_start;
    QueryClock::duration  m_lock{};
    bool                  m_hit     = false;
    bool                  m_skipped = false;
};

/// @brief Called by EndQueryFrame for every live tag.
using QueryFrameVisitor = void (*)(void* user, const QueryFrameUse& use);

/// @brief Closes the frame for every live tag (QueryStats::EndFrame), handing each tag's use to `visit` (may be null).
/// @pre Once per frame, from one thread (the controller's load report, after the present).
void EndQueryFrame(QueryFrameVisitor visit, void* user);

/// @return every live tag's use since the last call (each reset), those with calls only, most time first.
/// @pre One thread (the controller's load report).
std::vector<QueryWindow> TakeQueryWindows();

/// @brief The report's text for `windows` over `frames` frames: per frame, each tag's calls and milliseconds, its worst
///        query, its lock wait, the queries the backend skipped, and the frames it went over budget.
std::string FormatQueryWindows(std::span<const QueryWindow> windows, std::uint64_t frames);

}  // namespace CB::platform::physics
