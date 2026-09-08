#pragma once

// Community Behaviors — RUNTIME graph-variable trace (the in-game counterpart of the compile trace).
//
// Where the compile trace (havok::model::trace) makes the OFFLINE compile greppable, this makes the
// LIVE execution greppable: each frame it samples the behavior-graph variables a debug/*.yaml probe
// names in its `watch:` list off the player and logs every CHANGE to a greppable file — so a bug of
// the "a graph variable flips at the wrong moment" class (CB-2: bAnimationDriven dropping to 0 mid-
// attack -> the ice-skate) becomes a log line with a timestamp instead of an eyeballed HUD.
//
// Opt-in via [Debug] bRuntimeTrace in settings.ini (renders in the converter's Debug tab, see
// CB::debug::kFlags). ZERO COST WHEN OFF: Enabled() is a single atomic load; Sample() returns
// immediately. Sampling piggybacks the SMF present hook (DebugOverlay), so it needs SKSE Menu
// Framework present — the same dependency the locomotion HUD already carries.
//
// Record format (stable, greppable), written to Data\community_behaviors\runtime_trace.log:
//   [RT] <ms> <var> <kind> <old> -> <new>
// e.g.
//   [RT] 48213 bAnimationDriven int 1 -> 0
//   [RT] 48213 BFCO_IsBlocking  int 1 -> 0
// The first observation of a variable logs "(none) -> <v>"; when a variable stops resolving (graph
// swapped / actor unloaded) it logs "<v> -> (n/a)".

namespace CB::RuntimeTrace {

    // Read [Debug] bRuntimeTrace; if on, load the watch list from the deployed debug/ probes and open
    // the log. Call once at kDataLoaded (before DebugOverlay::Install, order-independent). No-op when off.
    void Install();

    // True once Install armed the trace (flag on + at least one watched variable). A plain atomic load.
    bool Enabled() noexcept;

    // Sample every watched variable off the live player graph and log the ones that changed. Call once
    // per frame (DebugOverlay's present hook). No-op when disabled.
    void Sample();

}  // namespace CB::RuntimeTrace
