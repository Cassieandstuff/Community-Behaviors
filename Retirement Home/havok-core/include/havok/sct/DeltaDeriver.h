#pragma once
// DeltaDeriver — derive a per-mod behavior DELTA from a mod's LOOSE, precompiled full
// behavior graph (the graph shipped as a finished .hkx, NOT as a Nemesis patch — e.g.
// HorsePower's actors/horse/behaviors/horsebehavior.hkx, BFCO's loose graphs). BR only
// ingests deltas, so such a loose full graph is otherwise invisible and its changes drop.
//
// The derive is the inverse of the runtime merge: decompile the mod graph AND the vanilla
// base, MATCH each node by a structural (owner-qualified) identity — numbering is unrelated
// between two independent compiles, so names + structure are the only stable identity — and
// emit the mod's nodes keyed by the BASE's own encounter-order NUMERIC ids (matched) or a
// fresh id (new). Numeric base ids mean the delta drops straight onto the shipped Skyrim.hky
// base AND sidestep the compiler's id!=name string-id node drop. Only the nodes that DIFFER
// from vanilla (plus new nodes) are written — unchanged nodes come from the base at runtime.
//
// Gated by the round-trip: merge(base, delta) reproduces the mod graph (the one-way derive's
// correctness oracle). Validated on HorsePower's horse: 349/351 nodes, 0 dropped.

#include <string>

namespace havok::sct {

struct DeriveDeltaResult {
    bool ok = false;
    std::string error;
    int matched = 0;          // mod nodes matched to a base id (by structural identity)
    int added = 0;            // brand-new mod nodes (no vanilla counterpart)
    int changedNodes = 0;     // delta node files written because they differ from vanilla
    int newNodes = 0;         // delta node files written because they are new
    int removedFromBase = 0;  // vanilla nodes with no mod counterpart (informational)
    long baseMaxId = 0;       // highest base id (new nodes are numbered above it)
};

// vanillaBin  : the pristine vanilla behavior binary (the base graph, keyed by the same
//               encounter-order numbering BuildBaseBundle's DecompileBehaviorTree assigns)
// fullModBin  : the mod's loose, precompiled full behavior binary
// outDeltaDir : the changed + new node YAML is written here (the mod's .hky delta subtree)
DeriveDeltaResult DeriveLooseBehaviorDelta(const std::string& vanillaBin,
                                           const std::string& fullModBin,
                                           const std::string& outDeltaDir);

}  // namespace havok::sct
