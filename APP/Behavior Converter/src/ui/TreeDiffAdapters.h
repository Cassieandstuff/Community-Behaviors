#pragma once
// TreeDiffAdapters (STUB) — the old cb-tree-diff engine was retired in the org-pass firesale: it was
// built before the schema stack + the linker/BoneMembrane and pulled the quarantined typed havok-core
// back into the converter (typed DecompileToDir + typed skeleton import), the last thing blocking the
// havok-core cut. The Diff tab is a thin front-end over it, so rather than port a doomed engine it is
// stubbed here while a ground-up Pandora-vs-CB diff tool is built in its own session — one that layers
// Pandora OVER vanilla (so the skeleton is present and BoneMembrane resolves bone names with no manual
// --skeleton) and decompiles both sides through the one schema emitter.
//
// This header keeps the SAME public types so ui/ConverterUI compiles unchanged; RunTreeDiff is a no-op
// that reports the tab is temporarily unavailable. Replace with the new tool's adapter when it lands.

#include <functional>
#include <string>

namespace havok::diff {

struct TreeDiffOptions {
    std::string a;
    std::string b;
    std::string deltaDir;
    std::string domain = "auto";
    std::string skeleton;
    std::string schema;
};

struct TreeDiffOutcome {
    bool        ok = false;
    std::string error;
    bool        anyDifference = false;
    int         artifacts = 0;
    int         comparedRecords = 0;
    int         differing = 0;
    int         onlyA = 0;
    int         onlyB = 0;
    std::string summaryPath;
};

using LogFn = std::function<void(const std::string&)>;

// Stub: the tree-diff engine is being rewritten (Pandora-over-vanilla). Reports unavailable.
inline TreeDiffOutcome RunTreeDiff(const TreeDiffOptions&, const LogFn& log) {
    if (log) log("tree-diff is being rewritten (Pandora-over-vanilla, schema-native) — this tab is temporarily unavailable.");
    TreeDiffOutcome out;
    out.ok    = false;
    out.error = "The Diff engine is being rewritten as a ground-up Pandora-vs-CB tool. This tab is temporarily unavailable.";
    return out;
}

}  // namespace havok::diff
