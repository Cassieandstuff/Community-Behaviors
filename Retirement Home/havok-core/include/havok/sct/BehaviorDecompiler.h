#pragma once
// BehaviorDecompiler — public entry points. The normal full decompile lives behind
// DecompileToDir (CharacterDecompiler.h); these two are the seams the native per-mod
// delta converter (PatchConverter) drives, reusing the SAME per-class YAML emit as a
// normal decompile so the delta shape can never drift from the loader's format.

#include "havok/sct/CharacterDecompiler.h"   // DecompileResult

#include <filesystem>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace havok { class hkbBehaviorGraph; }

namespace havok::sct {

struct BoneNameTable;   // havok/sct/BoneNames.h — optional skeleton for bone-index -> name

// Full decompile of a behavior graph to a source tree (behavior.yaml +
// data/graphdata.yaml + per-node YAML). When `stableIds` is non-null it overrides the
// encounter-order numeric node ids (a vanilla object -> its tagfile-#NNNN string), so a
// vanilla BASE bundle emits the SAME ids the per-mod deltas reference. Null = the
// original numeric ids (unchanged).
// `outIds` (optional) receives the id string each emitted object was given (the stableId
// when provided, else the encounter-order number) — so a caller can learn the numbering a
// null-stableIds base decompile assigned, then key a derived delta to those same ids.
DecompileResult DecompileBehaviorTree(
    const std::shared_ptr<havok::hkbBehaviorGraph>& bg,
    const std::filesystem::path& dir,
    const std::unordered_map<const void*, std::string>* stableIds = nullptr,
    std::unordered_map<const void*, std::string>* outIds = nullptr,
    const BoneNameTable* bones = nullptr);

// Emit a native per-mod .hky DELTA from the vanilla+mod merged graph `bg`: decompile
// the whole graph with `stableIds` for every ref/filename, but write ONLY the nodes
// whose stable id is in `deltaIds` (this mod's override #NNNN + new mod$N) — unchanged
// vanilla nodes come from the base bundle at runtime. Also writes data/additive.yaml
// for the mod's added vocabulary (`added{Event,Variable,CharProp}Names`, whose full
// type/flags are pulled from bg's graph data). Overrides are emitted as FULL nodes:
// the merged graph is vanilla + only the mod's real changes, so at runtime rymlEqual
// filters the unchanged params and only the real deltas apply (no stale-copy revert).
// A changed id with no YAML node of its own (an owner-INLINED sub-object: a state/SM's
// transition or notify array, a transition's condition, a clip's trigger array, a
// blend child / bone-weight array, a binding set) is folded into its OWNING node,
// which is emitted instead. `warnings` (optional) receives a line for any changed id
// that could not be mapped — a silent drop is never allowed to pass unreported.
DecompileResult DecompileNativeDelta(
    const std::shared_ptr<havok::hkbBehaviorGraph>& bg,
    const std::unordered_map<const void*, std::string>& stableIds,
    const std::set<std::string>& deltaIds,
    const std::set<std::string>& addedEventNames,
    const std::set<std::string>& addedVariableNames,
    const std::set<std::string>& addedCharPropNames,
    const std::filesystem::path& outDir,
    std::vector<std::string>* warnings = nullptr);

} // namespace havok::sct
