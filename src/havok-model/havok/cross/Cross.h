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

// (hkVector4/Quaternion text parser moved to <common/Vec4Text.h> — havok::vec4::parseVec4.)

// (enum value → name moved to <havok/model/HavokEnums.h> — the whole enum leaf, encode + both
// decodes, is one authority there now. This header keeps only the contextual name↔index lookups.)

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

// ── animation track -> bone index (the INVERSE membrane) ─────────────────────
// Resolve an animation track's authored bone reference to a skeleton bone index.
// This is the animation-side twin of the graph's hkbBoneIndexArray resolution: an
// hkaAnimationBinding maps each transform track to a bone index, and CB authors
// that map by NAME so a clip follows the served skeleton (bones added by the
// membrane shift indices; the clip re-resolves) instead of freezing raw indices.
//
// `ref` is either a bone NAME (resolved via boneIndexByName against the served
// skeleton) or the "track<N>" placeholder the no-skeleton decompile emits, which
// means "identity" — track N animates bone N. Returns `ordinal` (identity) when
// there is no skeleton, when ref is the matching track<ordinal> placeholder, or
// when a name does not resolve (caller keeps identity rather than dropping the
// track). So a null/empty skeleton round-trips to the identity binding vanilla ships.
inline int trackBoneRef(std::string_view ref, int ordinal,
                        const std::vector<std::string>& boneNames) {
    // "track<N>" placeholder -> identity index N (the decompile-without-skeleton form).
    if (ref.size() > 5 && ref.substr(0, 5) == "track") {
        int n = 0; bool allDigits = true;
        for (char c : ref.substr(5)) { if (c < '0' || c > '9') { allDigits = false; break; } n = n * 10 + (c - '0'); }
        if (allDigits) return n;
    }
    if (boneNames.empty()) return ordinal;             // no skeleton -> identity
    const int idx = boneIndexByName(ref, boneNames);
    return idx >= 0 ? idx : ordinal;                    // unresolved name -> keep identity
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

}  // namespace havok::cross
