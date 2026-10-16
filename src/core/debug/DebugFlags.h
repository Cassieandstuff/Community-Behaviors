#pragma once
// DebugFlags.h — the CANONICAL registry of Community Behaviors' runtime debug / diagnostic
// toggles. ONE place that enumerates every settings.ini flag and marker-file switch the plugin
// honors, so tooling (the Behavior Converter's Debug tab) renders them as toggles AUTOMATICALLY
// and nothing has to be listed twice.
//
// Dependency-free by design (plain data — no CommonLib, RE, or PCH), so the converter, which
// shares none of the plugin's engine bindings, can include this header directly.
//
// ADDING A FLAG: add ONE row to kFlags below, then wire its BEHAVIOR in the plugin (a flag
// inherently DOES something at runtime — that per-flag logic lives with the code it gates and
// can't be generated). The converter's Debug tab picks up the toggle with no further edits.

#include <array>
#include <string_view>

namespace CB::core::debug {

    enum class FlagKind {
        IniBool,     // a [section] key=true/false in Data/SKSE/Plugins/Community Behaviors/settings.ini
        MarkerFile,  // presence of a file under Data/community_behaviors/ (present == on)
    };

    struct Flag {
        std::string_view id;       // stable identifier (UI ##id + log key)
        FlagKind         kind;
        std::string_view section;  // ini section (IniBool); empty for MarkerFile
        std::string_view key;      // ini key (IniBool) OR marker filename (MarkerFile)
        bool             defOn;    // value when the key/marker is absent
        /// @brief defOn for a DEBUG (!NDEBUG) plugin build — differs from defOn only for a build-dependent default.
        /// @note The converter can't know which plugin build is installed, so it shows both.
        bool             defOnDebug;
        std::string_view label;    // UI label
        std::string_view help;     // one-line tooltip
    };

    // The registry. Array order == display order in the Debug tab.
    /// @note The plugin reads a flag only through FlagOn("<id>") (core/debug/Settings.h), which resolves the row's
    ///       section/key/kind/default — no reader carries its own copy of the strings.
    /// @note force_regenerate: the warm-up recompile is a pure optimization whose output persists on disk, so a
    ///       release build reuses it (fast start) while a debug build regenerates (the cache can't drift while
    ///       iterating on the converter/bundles). The shipped settings.ini leaves the key commented so the build
    ///       default applies until someone opts in.
    /// @note er_gate stays default-off in every build until the gate is verified in-engine (a fault blocks all
    ///       wildcards and T-poses every actor); unrelated to the watermark, which is always on.
    inline constexpr auto kFlags = std::to_array<Flag>({
        { "force_regenerate", FlagKind::IniBool, "Cache", "bForceRegenerate", false, true,
          "Force cache regenerate",
          "Recompile the whole behavior cache at launch instead of reusing the on-disk one. "
          "Build default differs (debug on / release off); an explicit key overrides either way." },
        { "er_gate", FlagKind::IniBool, "ERGate", "bEnable", false, false,
          "Engine Relay wildcard gate",
          "Inject BR_ERWildcardLock and gate every global wildcard on it (for Engine Relay to flip "
          "per-actor). Unproven — a broken gate blocks every wildcard and T-poses every actor." },
        { "conditions", FlagKind::IniBool, "Conditions", "bEnable", false, false,
          "OAR-parity conditions",
          "Register the built-in condition roster and install the conditional-setdata producer/matcher "
          "engine hooks. Unproven in a shipped build and inert until conditional replacements exist; "
          "off = no engine hooks installed." },
        { "noskeletonserve", FlagKind::MarkerFile, "", "noskeletonserve.enable", false, false,
          "Disable skeleton serve",
          "Skip serving CB's compiled skeleton.hkx (skeleton opens fall through to vanilla) — a "
          "diagnostic opt-out." },
        { "sequencer_disable", FlagKind::MarkerFile, "", "sequencer.disable", false, false,
          "Serial animation compile",
          "Compile native animations one at a time instead of across a worker pool — an escape hatch for "
          "a machine that shows trouble with the parallel compile (its output is byte-identical)." },
        { "havok_runtime", FlagKind::IniBool, "HavokRuntime", "bEnable", true, true,
          "Havok runtime takeover",
          "Run CB's reimplemented Havok behavior runtime in place of vanilla's (per class: [HavokRuntime] "
          "<Class>=off, or sOnly=<Class>,...). Inert without community_behaviors/runtime/havok-runtime.cbimg." },
        { "havok_activate_counter", FlagKind::IniBool, "Debug", "bHavokActivateCounter", true, true,
          "Clip activate counter",
          "Count hkbClipGenerator::activate calls through the Havok runtime's cell table, logging at every power "
          "of two — the proof that features can override reimplemented Havok functions. Observes only." },
        { "runtime_trace", FlagKind::IniBool, "Debug", "bRuntimeTrace", false, false,
          "Runtime graph-variable trace",
          "Log live behavior-graph variable values (the classes/vars a debug/*.yaml probe names via its "
          "watch: list) per-frame ON CHANGE to a greppable file." },
    });

}  // namespace CB::core::debug
