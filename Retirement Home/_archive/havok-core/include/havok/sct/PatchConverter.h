#pragma once
#include <string>
#include <vector>

// Nemesis/Pandora patch(es) -> merged binary hkx, at the OBJECT level, with a
// Bash-style multi-mod field/array merge.
//
// The offline converter for Community Behaviors's Stage-4 migration (spec §11): read
// one OR MORE mods' per-behavior patch directories (`#NNNN.txt` overrides +
// `#mod$N.txt` new nodes) IN LOAD ORDER, overlay them onto the shipped vanilla
// behavior graph keyed by the tagfile #NNNN the structural-alignment oracle
// recovers (spec §4.4), and re-serialize the merged object graph. Object-level
// (not by-file) because the decompiler inlines helper objects while the patch keys
// them as standalone nodes — only object identity reconciles the two.
//
// Multi-mod merge (the "built-in bashed patch"): when several mods override the
// same #NNNN, each mod's node is diffed against VANILLA and only its real deltas
// apply — scalar/pointer fields last-writer-wins, array fields UNION (each mod's
// added items accumulate). So a later mod's untouched (vanilla-valued) fields never
// revert an earlier mod's edit, and two mods appending to the same array (e.g. clip
// triggers, or event-name tables) both survive.
//
// Overrides mutate the vanilla object IN PLACE (preserving pointer identity for
// every holder, so no reference rewiring); new nodes are created and wired in via
// their referrers. Symbolic $eventID[Name]$ / $variableID[Name]$ fields resolve
// against the merged string-data name tables.

namespace havok::sct {

struct PatchConvertResult {
    bool ok = false;
    std::string error;                          // set when ok == false
    int overrides = 0;                          // distinct #NNNN nodes overridden
    int added = 0;                              // #mod$N new nodes created
    int mergedConflicts = 0;                    // #NNNN overridden by >1 mod (bashed)
    int symbolsResolved = 0;                    // $eventID/$variableID occurrences resolved
    int refsUnresolved = 0;                     // refs that resolved to null (reported, not fatal)
    int skippedMismatch = 0;                    // overrides skipped: aligned class != patch class
                                                // (usually vanilla binary/tagfile source drift)
    std::vector<std::string> unsupportedClasses;// distinct patch classes with no populator yet
    std::vector<std::string> warnings;
};

// vanillaBin : shipped vanilla behavior binary (e.g. 0_master.hkx)
// vanillaXml : the matching vanilla tagfile XML (oracle alignment + merge base)
// patchDirs  : one or more mods' per-behavior patch dirs, IN LOAD ORDER (later wins)
// outBin     : merged binary written here (unless a native-emit dir is set below)
//
// Native emit (skips the merged-binary output — for the runtime per-mod merge model):
//   nativeDeltaDir : emit ONE mod's native .hky DELTA here (pass a single patchDir) —
//                    overrides as full nodes + new mod$N nodes + data/additive.yaml,
//                    keyed by the stable tagfile id.
//   vanBaseDir     : emit the vanilla BASE .hky here (pass NO patchDirs) — a full
//                    decompile keyed by the same tagfile ids the deltas reference.
// Deltas from many mods merge onto the one base at runtime (YamlBehaviorLoader::LoadMerged).
PatchConvertResult ConvertPatch(const std::string& vanillaBin,
                                const std::string& vanillaXml,
                                const std::vector<std::string>& patchDirs,
                                const std::string& outBin,
                                const std::string& nativeDeltaDir = "",
                                const std::string& vanBaseDir = "");

}  // namespace havok::sct
