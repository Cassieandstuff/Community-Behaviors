#pragma once
// ---------------------------------------------------------------------------
// havok::cross — the cross-kind MEMBRANE.
//
// The behavior pipeline crosses between a human/name/string representation
// (.hky YAML) and a raw/index/typed one (binary Havok) in three layers: the
// SCHEMA converter (havok-model), the BUILD side (BehaviorBuilder), and the
// EMIT side (BehaviorDecompiler). Each crossing — bone name↔index, event /
// variable / character-property name↔index, enum name↔int, hkVector4 string↔
// bytes — was historically re-implemented independently in each layer and,
// within a layer, per class. That scatter is what the typed havok-core classes
// were, reborn; this header is the single home so the rule for each crossing is
// written ONCE and every layer calls it.
//
// Representation-agnostic on purpose: the resolvers take the roster name-lists
// (bone list, event/variable/property rosters) rather than a model object, so
// havok-core BUILD/EMIT and havok-model SCHEMA can all share them without a
// dependency cycle (header-only, mirrors BashMerge.h).
//
// Incremental adoption: bone name↔index first (it had THREE identical copies —
// BehaviorBuilder setBone / buildBoneWeights / buildBoneIndexArray). Rosters,
// enum/flags, and vec4 codecs fold in next, then the schema-declared CrossKind
// dispatch.
// ---------------------------------------------------------------------------

#include <array>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace havok::cross {

// ── hkVector4 / Quaternion text ↔ 4 floats ───────────────────────────────────
// The ONE vec4 text parser. Accepts BOTH the decompiler's "(x y z w)" and a
// vanilla/Nemesis bare, possibly multi-line "x\n y\n z\n w" (no parens). This is
// the single source that BehaviorBuilder::pv4 (raw->Vector4) and havok-model
// parseVec4Raw (raw->16 bytes) both call, killing the divergent-twin hazard (B4,
// where the paren-only parser silently zeroed a bare vec4 — BR-28).
inline std::array<float, 4> parseVec4(std::string_view t) {
    std::string s(t);
    for (char& c : s) if (c == '(' || c == ')' || c == ',') c = ' ';
    std::array<float, 4> q{ 0.f, 0.f, 0.f, 0.f };
    std::istringstream ss(s);
    ss >> q[0] >> q[1] >> q[2] >> q[3];   // >> skips whitespace incl. newlines
    return q;
}

// ── enum value → name (deterministic reverse lookup) ─────────────────────────
// The inverse of enums::ResolveEnum: given a numeric value + its name→value
// table, return the name. DETERMINISTIC — when a value has multiple names
// (aliases), the lexicographically smallest is chosen, so the emitted symbol is
// stable across runs/compilers (the old per-file `revNum` iterated an
// unordered_map and returned an arbitrary match — B5). Empty string if no name
// maps to the value (caller then emits the raw number).
inline std::string enumName(long value, const std::unordered_map<std::string, long>& table) {
    const std::string* best = nullptr;
    for (const auto& [name, val] : table)
        if (val == value && (best == nullptr || name < *best)) best = &name;
    return best ? *best : std::string();
}

// ── bone name ↔ skeleton index ──────────────────────────────────────────────
// Resolve a bone NAME to its index in the skeleton bone list, or -1 if absent.
// The single scan that BehaviorBuilder's setBone / buildBoneWeights /
// buildBoneIndexArray each used to inline. Callers own the policy around it
// (numeric passthrough, the BR-10 throw-on-miss, the empty-skeleton guard) —
// this only answers "which index is this name," so the error CONTEXT stays at
// the call site while the lookup lives here once.
inline int boneIndexByName(std::string_view name, const std::vector<std::string>& boneNames) {
    for (std::size_t i = 0; i < boneNames.size(); ++i)
        if (boneNames[i] == name) return static_cast<int>(i);
    return -1;
}

// Inverse: a bone index -> its skeleton name, or "" when there is no skeleton
// loaded or the index is out of range (the caller then keeps the raw number).
inline std::string boneNameByIndex(int index, const std::vector<std::string>& boneNames) {
    if (index < 0 || index >= static_cast<int>(boneNames.size())) return {};
    return boneNames[static_cast<std::size_t>(index)];
}

// ── graph roster name ↔ index (events / variables / character properties) ─────
// The rosters are the graph's ordered name lists (hkbBehaviorGraphStringData's
// eventNames / variableNames / characterPropertyNames). Same shape for all three,
// so ONE pair of primitives serves them all — the shared core under the build's
// ResolveMaps, the emit's eventName/variableName/charPropName, and the schema's
// NameResolver, which each used to reimplement this lookup.
//
// index -> name: "" when out of range (caller keeps the raw number). This is the
// raw lookup; a "first-occurrence only" emit policy (dup-guard) layers on top.
inline std::string rosterName(int index, const std::vector<std::string>& roster) {
    if (index < 0 || index >= static_cast<int>(roster.size())) return {};
    return roster[static_cast<std::size_t>(index)];
}

// name -> index by linear scan, or -1 if absent. For hot paths that resolve many
// names against one roster, build an unordered_map once instead (see Roster) —
// this is the simple form for one-shot lookups + the shared scan rule.
inline int rosterIndex(std::string_view name, const std::vector<std::string>& roster) {
    for (std::size_t i = 0; i < roster.size(); ++i)
        if (roster[i] == name) return static_cast<int>(i);
    return -1;
}

}  // namespace havok::cross
