#pragma once

// The compile gate — the ONE place the cold behavior compile + serve arming happens now.
//
// It is NOT run at plugin load (no renderer up, and the old synchronous-at-load move only ever
// bought a race the engine's own single-threaded load order already wins for us). Instead the
// engine's FIRST open of animationsetdatasinglefile.txt / animationdatasinglefile.txt — or the
// first resolve of a BR-owned behavior — calls EnsureCompiledAndArmed() from INSIDE that detour.
// The engine's loader thread is then physically parked in our hook until the compile returns, so
// nothing downstream (graph arming, the later setdata/adsf reads, byteserve) can run against a
// still-vanilla graph: correctness by ordering, not by timing. The exact same pattern the
// per-project loader gate already proved (AnimationDataServer InstallPerProjectGate).
//
// Because that first open happens during game-data load — AFTER kDataLoaded — SMF's present hook
// and the render thread are live, so the compile thread's SetProgress drives a real on-screen
// progress overlay while the loader thread blocks here. That is the whole point: the progress bar
// is back AND the race is won, at once.
namespace CB {

    // One-shot, blocking, thread-safe. First caller arms the serve. Two shapes:
    //   • Default (proven) path: warm → arm from disk; cold → run the 64MB compile + materialize and
    //     JOIN it here, then serve+arm adsf/setdata. Fully synchronous; no progress bar possible.
    //   • Split path (DEFAULT for a cold compile; opt out with Data\community_behaviors\progressbar.disable,
    //     and it's forced off when the adsf-derive feature needs the compile's clip sink): serve+arm
    //     adsf/setdata synchronously (fast, independent of the graph compile), then LAUNCH the cold
    //     compile on a background thread and RETURN — so the game reaches its menu and presents while the
    //     compile runs, and the passive present-hook bar (ProgressHud) is visible. byteserve's backstop
    //     calls WaitForCompile() before serving an owned graph, so a graph is never served half-compiled.
    // Every later call — and any reentrant call on a doing thread — returns immediately.
    void EnsureCompiledAndArmed();

    // Block until the background (split-path) compile has finished and the serve is armed. No-op on the
    // default path (the compile already joined) or once the compile is done. Called by byteserve before
    // it serves a BR-owned graph, so correctness never depends on the bar's timing.
    void WaitForCompile();

}  // namespace CB
