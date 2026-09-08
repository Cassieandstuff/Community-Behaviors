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
//
// The plugin's own readers (Plugin.cpp ReadAdsfFromFeature/…, AnimDataProbe's MarkerPresent,
// Resolver's noskeletonserve check) are the behavior side; keep their section/key strings in sync
// with the rows here (or migrate them to reference these rows — see the registry note).

#include <array>
#include <string_view>

namespace CB::debug {

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
        std::string_view label;    // UI label
        std::string_view help;     // one-line tooltip
    };

    // The registry. Array order == display order in the Debug tab.
    inline constexpr std::array<Flag, 12> kFlags{ {
        { "force_regenerate", FlagKind::IniBool, "Cache", "bForceRegenerate", false,
          "Force cache regenerate",
          "Recompile the whole behavior cache at launch instead of reusing the on-disk one. "
          "Build default differs (debug on / release off); an explicit key overrides either way." },
        { "use_schema", FlagKind::IniBool, "Compiler", "bUseSchema", true,
          "Data-driven schema compiler",
          "Use the schema-driven compiler (default on). Off falls back to the typed backbone." },
        { "adsf_from_feature", FlagKind::IniBool, "Compiler", "bAdsfFromFeature", false,
          "adsf-derive feature",
          "Derive animationdata straight off the compiled graph via the contributor feature — a "
          "path parallel to the proven collated merge (validation-only unless it drives the emit)." },
        { "adsf_roster_from_scan", FlagKind::IniBool, "Compiler", "bAdsfRosterFromScan", false,
          "adsf roster from scan",
          "Source the adsf per-project asset roster (the paths func3 enumerates) from the hky scan "
          "instead of an authored index.yaml. Additive/union onto the canonically-cased base." },
        { "er_gate", FlagKind::IniBool, "ERGate", "bEnable", false,
          "Engine Relay wildcard gate",
          "Inject BR_ERWildcardLock and gate every global wildcard on it (for Engine Relay to flip "
          "per-actor). Unproven — a broken gate blocks every wildcard and T-poses every actor." },
        { "perproject_animdata", FlagKind::MarkerFile, "", "perproject.enable", false,
          "Per-project animdata loader",
          "Route the engine to the per-project animdata form (Meshes\\AnimationData\\...) instead of "
          "the collated blob. Retail-untested — breaks root motion + paired anims in a real load order." },
        { "syncprobe", FlagKind::MarkerFile, "", "syncprobe.enable", false,
          "Sync-clip probe",
          "Log every paired/synchronized clip as it activates (dialogue-idle T-pose / mount-jitter diag)." },
        { "probe_animdata", FlagKind::MarkerFile, "", "probe_animdata.txt", false,
          "AnimationClipData probe (read)",
          "Read locomotion clip motionSpeed/duration/triggers from the live AnimationClipDataSingleton "
          "under both the .br and stock keys (the BR-1 ice-skate bisector)." },
        { "inject_animdata", FlagKind::MarkerFile, "", "inject_animdata.txt", false,
          "AnimationClipData probe (write)",
          "As well as the read probe, write a distinctive motionSpeed into the singleton and read it "
          "back — proof the in-engine clip table is writable." },
        { "noskeletonserve", FlagKind::MarkerFile, "", "noskeletonserve.enable", false,
          "Disable skeleton serve",
          "Skip serving CB's compiled skeleton.hkx (skeleton opens fall through to vanilla) — a "
          "diagnostic opt-out." },
        { "compile_trace", FlagKind::IniBool, "Debug", "bCompileTrace", false,
          "Compile trace (schema-driven probes)",
          "Emit a name-annotated, greppable trace of the behavior compile to the log, gated by the "
          "probe definitions in the deployed Havok/core/Schema/debug/*.yaml (edit those to steer it)." },
        { "runtime_trace", FlagKind::IniBool, "Debug", "bRuntimeTrace", false,
          "Runtime graph-variable trace",
          "Log live behavior-graph variable values (the classes/vars a debug/*.yaml probe names via its "
          "watch: list) per-frame ON CHANGE to a greppable file — the in-game counterpart of compile trace." },
    } };

}  // namespace CB::debug
