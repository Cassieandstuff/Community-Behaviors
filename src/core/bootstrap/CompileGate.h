#pragma once

// The compile gate — the ONE place the cold behavior compile + serve arming happens now.
//
// It is NOT run at plugin load (no renderer up, and the old synchronous-at-load move only ever
// bought a race the engine's own single-threaded load order already wins for us). Instead the
// engine's FIRST open of animationsetdatasinglefile.txt / animationdatasinglefile.txt — or the
// first resolve of a BR-owned behavior — calls EnsureCompiledAndArmed() from INSIDE that detour.
//
// The gate no longer BLOCKS the calling thread on the whole compile: it arms the progress bar, kicks a
// single background ArmThread that does all the heavy work (WaitReady on Init + arm graphs/adsf), and
// RETURNS immediately. That keeps the MAIN thread (which is where the early animationdata open lands)
// free to pump the window and present — so the game isn't minimized with a trapped cursor and the bar
// can actually paint. Two per-open waits provide correctness without a main-thread stall:
//   • the animationdata/animationsetdata detours call WaitAdsfArmed() — on a warm cache the ArmThread
//     arms those redirects FIRST (instant, Init-independent), so this is milliseconds, not ~50s;
//   • byteserve's owned-graph backstop calls WaitForCompile() — those opens are on loader threads, so
//     parking them until materialize is fine and a graph is never served half-compiled.
//
// The first open happens during game-data load — AFTER kDataLoaded — so SMF's present hook and the
// render thread are live; the ArmThread's SetProgress drives the on-screen bar over the (now live) menu.
namespace CB {

    // One-shot, thread-safe, NON-blocking KICK. First caller arms the bar and spawns the background
    // ArmThread (which arms adsf-from-cache first on warm, waits Init, then arms graphs); returns at
    // once. Every later call — and any reentrant call on the doing thread — returns immediately.
    void EnsureCompiledAndArmed();

    // Block until the adsf/setdata redirects are armed (bounded timeout, then vanilla passthrough).
    // Called by the animationdata/animationsetdata detours after they kick the gate. Fast on warm.
    void WaitAdsfArmed();

    // Block until the background arm/compile has finished and the graph serve is armed (materialize).
    // Called by byteserve before it serves a BR-owned graph, so correctness never depends on the bar.
    void WaitForCompile();

}  // namespace CB
