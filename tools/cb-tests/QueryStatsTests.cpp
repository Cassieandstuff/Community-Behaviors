/// @file
/// @brief cb-tests over platform/physics QueryStats: the per-tag physics query counters, the report window, the frame
///        budget, and the report text.

#include "TestSupport.h"

#include <platform/physics/QueryStats.h>

#include <chrono>
#include <string>
#include <thread>
#include <vector>

namespace CB::config::tests {

namespace {

namespace pp = CB::platform::physics;

bool Has(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

void TestCounters()
{
    pp::QueryStats tag("test.counters");
    tag.Record(1000, 100, true);
    tag.Record(3000, 0, false);
    tag.Record(2000, 400, true);
    tag.Record(10, 0, false, true);
    const pp::QueryWindow w = tag.TakeWindow();
    Check(w.calls == 4 && w.hits == 2 && w.skipped == 1, "a window counts calls, hits and skipped queries");
    Check(w.nanos == 6010 && w.maxNanos == 3000, "a window sums time and keeps the worst query");
    Check(w.lockNanos == 500 && w.maxLockNanos == 400, "a window sums the lock wait and keeps the worst");
    Check(std::string(w.name) == "test.counters", "a window carries its tag's name");
    const pp::QueryWindow next = tag.TakeWindow();
    Check(next.calls == 0 && next.nanos == 0 && next.maxNanos == 0 && next.maxLockNanos == 0 && next.skipped == 0, "taking a window resets it");
}

void TestThreads()
{
    pp::QueryStats           tag("test.threads");
    std::vector<std::thread> threads;
    for (int t = 0; t < 4; ++t)
        threads.emplace_back([&tag, t] {
            for (int i = 0; i < 10000; ++i) tag.Record(static_cast<std::uint64_t>(1 + t), 0, (i & 1) == 0);
        });
    for (std::thread& t : threads) t.join();
    const pp::QueryWindow w = tag.TakeWindow();
    Check(w.calls == 40000 && w.hits == 20000, "records from four threads all land");
    Check(w.nanos == 10000ull * (1 + 2 + 3 + 4) && w.maxNanos == 4, "concurrent sums and maxima are exact");
}

void TestTimer()
{
    pp::QueryStats tag("test.timer");
    {
        pp::QueryTimer timer(tag);
        timer.LockWaited(std::chrono::milliseconds(5));
        timer.Hit(true);
    }
    { pp::QueryTimer miss(tag); }
    {
        pp::QueryTimer skipped(tag);
        skipped.Skipped();
    }
    const pp::QueryWindow w = tag.TakeWindow();
    Check(w.calls == 3 && w.hits == 1 && w.skipped == 1, "a timer counts one query each, with its hit or skip");
    Check(w.lockNanos == 5'000'000 && w.maxLockNanos == 5'000'000, "a timer records the lock wait it is told");
}

void TestBudget()
{
    pp::QueryStats calls("test.budget.calls", { .callsPerFrame = 2 });
    calls.Record(10, 0, false);
    calls.Record(10, 0, false);
    pp::QueryFrameUse f = calls.EndFrame();
    Check(f.calls == 2 && !f.over && !f.firstOver, "a frame at its call budget is not over it");
    for (int i = 0; i < 3; ++i) calls.Record(10, 0, false);
    f = calls.EndFrame();
    Check(f.calls == 3 && f.nanos == 30 && f.over && f.firstOver, "the first frame over budget is flagged first");
    for (int i = 0; i < 5; ++i) calls.Record(10, 0, false);
    f = calls.EndFrame();
    Check(f.over && !f.firstOver, "a later frame over budget is over, but never first again (one log line)");
    f = calls.EndFrame();
    Check(f.calls == 0 && !f.over, "ending a frame resets its counts");
    Check(calls.TakeWindow().framesOver == 2, "the window counts the frames over budget");

    pp::QueryStats time("test.budget.ms", { .msPerFrame = 0.001 });
    time.Record(600, 0, false);
    Check(!time.EndFrame().over, "a frame under its time budget is not over it");
    time.Record(600, 0, false);
    time.Record(600, 0, false);
    Check(time.EndFrame().firstOver, "a frame over its time budget is");

    pp::QueryStats none("test.budget.none");
    for (int i = 0; i < 1000; ++i) none.Record(1'000'000'000, 0, false);
    Check(!none.EndFrame().over, "a tag with no budget is never over it");
}

void TestRegistry()
{
    struct Seen {
        int slow = 0, fast = 0, gone = 0;
        bool firstOver = false;
    } seen;
    pp::QueryStats slow("test.registry.slow", { .callsPerFrame = 1 });
    pp::QueryStats fast("test.registry.fast");
    {
        pp::QueryStats gone("test.registry.gone");
        gone.Record(1, 0, false);
    }
    slow.Record(5000, 0, true);
    slow.Record(5000, 0, true);
    fast.Record(100, 0, false);
    pp::EndQueryFrame(
        [](void* user, const pp::QueryFrameUse& use) {
            auto&             s    = *static_cast<Seen*>(user);
            const std::string name = use.stats->Name();
            if (name == "test.registry.slow") ++s.slow, s.firstOver = use.firstOver;
            if (name == "test.registry.fast") ++s.fast;
            if (name == "test.registry.gone") ++s.gone;
        },
        &seen);
    Check(seen.slow == 1 && seen.fast == 1, "EndQueryFrame visits every live tag once");
    Check(seen.gone == 0, "a destroyed tag leaves the registry");
    Check(seen.firstOver, "EndQueryFrame hands the visitor each tag's budget verdict");
    pp::EndQueryFrame(nullptr, nullptr);

    pp::QueryStats idle("test.registry.idle");
    const std::vector<pp::QueryWindow> windows = pp::TakeQueryWindows();
    bool                               sawIdle = false;
    for (const pp::QueryWindow& w : windows) sawIdle = sawIdle || std::string(w.name) == "test.registry.idle";
    Check(windows.size() >= 2 && std::string(windows[0].name) == "test.registry.slow", "windows come most time first");
    Check(!sawIdle, "a tag with no calls is left out of the report");
    Check(windows[0].framesOver == 1, "the report window carries the frames over budget");
    Check(pp::TakeQueryWindows().empty(), "taking the windows resets every tag");
}

void TestFormat()
{
    Check(pp::FormatQueryWindows({}, 10) == "none", "an empty report says none");
    pp::QueryWindow w;
    w.name         = "char.floor";
    w.calls        = 200;
    w.hits         = 150;
    w.nanos        = 2'500'000;
    w.maxNanos     = 45'000;
    w.lockNanos    = 100'000;
    w.maxLockNanos = 3'000;
    pp::QueryWindow camera;
    camera.name       = "camera.boom";
    camera.calls      = 10;
    camera.nanos      = 10'000;
    camera.framesOver = 3;
    camera.skipped    = 2;
    const std::vector<pp::QueryWindow> windows{ w, camera };
    const std::string                  text = pp::FormatQueryWindows(windows, 10);
    Check(Has(text, "char.floor 20.0x 0.250 ms (max 45 us, hits 75%; lock 0.010 ms, max 3 us)"), "per-frame calls, time, hits and lock: " + text);
    Check(Has(text, ", camera.boom 1.0x 0.001 ms") && Has(text, "; skipped 2; over budget 3 frames"), "skips and frames over budget are said: " + text);
    Check(!Has(text.substr(0, text.find("camera.boom")), "over budget") && !Has(text.substr(0, text.find("camera.boom")), "skipped"), "a tag never over budget or skipped says nothing of it: " + text);
}

}  // namespace

void RunQueryStatsTests()
{
    TestCounters();
    TestThreads();
    TestTimer();
    TestBudget();
    TestRegistry();
    TestFormat();
}

}  // namespace CB::config::tests
