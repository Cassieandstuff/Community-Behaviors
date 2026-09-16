#pragma once
// TreeDiffAdapters — the per-domain adapters + the public orchestrator behind the `tree-diff`
// tool (standalone exe + Behavior Converter "Diff" tab). Lifted out of the quarantined
// havok-core-cli into this APP-layer library so the tool no longer lives in a CLI verb.
//
// Each adapter turns ONE input artifact into a `RecordSet` (stable-key -> YAML text) using the
// SAME decompiler/decompose the rest of the pipeline uses, so both sides of a diff pass through
// one emitter and a formatting-only difference is impossible by construction (see TreeDiff.h).
// The domain-agnostic match/field-diff/delta-emit engine is TreeDiff.{h,cpp}; this file is the
// domain-aware layer that feeds it. See plans/zazzy-soaring-brook.md.

#include <functional>
#include <string>

namespace havok::diff {

// Inputs to one diff run. `domain` is "auto" (detected from A's extension/name) or an explicit
// override ("behavior"|"setdata"|"animdata"|"skeleton"|"character"). `skeleton` is an optional
// skeleton .hkx / bones.txt for bone-name resolution on the behavior/skeleton domains.
struct TreeDiffOptions {
    std::string a;
    std::string b;
    std::string deltaDir;
    std::string domain = "auto";
    std::string skeleton;
};

// Result of one diff run. `ok` is false on any setup/adapter error (with `error` set); when ok,
// `anyDifference` is the semantic verdict and the counts roll up the single-artifact diff. The
// exe/UI map this to the exit-code contract: identical=0, differences=1, usage/error=2.
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

// Progress sink: one line per call (no trailing newline required). Never null-checked by the
// caller side — pass a no-op if you don't want progress.
using LogFn = std::function<void(const std::string&)>;

// Run one diff: detect (or honor) the domain, build RecordSet A and B via the right adapter,
// DiffRecordSets, write the delta-only folder + summary.yaml, aggregate counts, log progress.
// `.hky` behavior input and pre-decompiled directories are refused (outcome.error set, ok=false)
// — the same-emitter invariant requires decompiling from binary here.
TreeDiffOutcome RunTreeDiff(const TreeDiffOptions& opts, const LogFn& log);

}  // namespace havok::diff
