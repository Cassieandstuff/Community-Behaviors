#pragma once

// BashMerge — the shared multi-mod field/array merge, extracted from PatchConverter
// so the offline converter (Nemesis XML -> merged binary) and the runtime loader
// (native YAML deltas -> merged BehaviorData) share ONE merge core and can never
// drift apart. The divergence-prone part — the per-param union/last-writer DECISION —
// is a single function (`decideParam`); each representation supplies thin mechanics.
//
// This header carries the xml::Node adapter (Havok-XML shape: a node's fields are
// `<hkparam name=..>` children). The ryml/YAML adapter used by YamlBehaviorLoader
// lives in that TU (ryml is isolated to it) and calls the SAME `decideParam`.

#include "havok/xml/Xml.h"

#include <algorithm>
#include <cstddef>
#include <functional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace havok::merge {

// ── generic (representation-agnostic) merge DECISION ────────────────────────────
enum class ParamMerge { Keep, LastWriter, ReplaceArray, UnionArray, GuardError };

// How a single param merges, given: is it an array; how many layers actually CHANGED
// it (differ from base); and whether it is a GUARDED positional node array (blender
// children / selector generators / modifier lists — order is load-bearing, so a
// multi-mod collision cannot be safely auto-unioned). Shared by BOTH adapters so the
// rule is defined exactly once:
//   * 0 changers                 -> Keep       (an untouched vanilla-valued field in a
//                                               later layer never reverts an earlier edit)
//   * scalar / pointer field     -> LastWriter (load order wins)
//   * array changed by ONE mod   -> ReplaceArray (the mod's array is its complete
//                                                  intended value; don't re-union vanilla)
//   * array changed by 2+ mods   -> UnionArray (each mod's vanilla-relative additions
//                                               accumulate — event/var tables, clip triggers)
//   * guarded array, 2+ mods     -> GuardError (refuse loudly; never silently drop)
inline ParamMerge decideParam(bool isArray, int changerCount, bool guarded) {
    if (changerCount == 0) return ParamMerge::Keep;
    if (!isArray)          return ParamMerge::LastWriter;
    if (changerCount == 1) return ParamMerge::ReplaceArray;
    return guarded ? ParamMerge::GuardError : ParamMerge::UnionArray;
}

// ── xml::Node adapter (a patch <hkobject>'s <hkparam> children) ─────────────────
// Deep structural equality of two nodes (tag + text + children, recursively).
// Ignores attributes so stale `numelements` never reads as a difference.
inline bool deepEqual(const xml::Node& a, const xml::Node& b) {
    if (a.tag != b.tag || a.text != b.text || a.children.size() != b.children.size()) return false;
    for (std::size_t i = 0; i < a.children.size(); ++i)
        if (!deepEqual(a.children[i], b.children[i])) return false;
    return true;
}
// An array-valued param: has numelements, or holds <hkobject>/<hkcstring> items.
inline bool isArrayParam(const xml::Node& p) {
    if (p.hasAttr("numelements")) return true;
    for (const auto& c : p.children) if (c.tag == "hkobject" || c.tag == "hkcstring") return true;
    return false;
}
// Non-empty whitespace tokens of a scalar/ref array param's text (e.g. "#712 #713").
inline std::vector<std::string> tokens(const std::string& s) {
    std::vector<std::string> v; std::stringstream ss(s); std::string t;
    while (ss >> t) v.push_back(t);
    return v;
}
// Union src's array items into dst (dst already holds base + prior layers), by value:
// an item is appended only if no equal item is already present. Handles struct/string
// item children and bare whitespace-token arrays.
inline void mergeArrayInto(xml::Node& dst, const xml::Node& src, const xml::Node* base = nullptr) {
    const bool hasObj = [&] {
        for (const auto& c : src.children) if (c.tag == "hkobject") return true;
        for (const auto& c : dst.children) if (c.tag == "hkobject") return true;
        return false;
    }();
    const bool hasStr = [&] {
        for (const auto& c : src.children) if (c.tag == "hkcstring") return true;
        for (const auto& c : dst.children) if (c.tag == "hkcstring") return true;
        return false;
    }();
    if (hasObj) {
        // <hkobject> child arrays are POSITIONAL and (for graphdata) run parallel to a *Names array —
        // variableInfos ⟷ variableNames, eventInfos ⟷ eventNames, characterPropertyInfos ⟷ …Names. Their
        // elements are VALUE-TYPES: many variables legitimately share {role:ROLE_DEFAULT, type:VARIABLE_TYPE_REAL},
        // so a by-VALUE dedup (deepEqual) silently DROPS a mod's real additions when an identical info already
        // exists — desyncing the infos from the string-unioned names. That is BR-37: TDM's 5 REAL vars
        // (TDM_Pitch/Roll/SpineRot/VelocityX/VelocityY, all {ROLE_DEFAULT,REAL}) collided with existing base
        // REAL infos, so variableInfos ended up 5 short of variableNames → EmitAdditiveVocab read them as the
        // BOOL default AND the runtime hkbVariableValueSet miscounted → a truncated variableValueSet pointer →
        // OAR char-setup rep-stosq CTD. Nemesis patches are base-prefixed, so append this changer's TAIL
        // additions (elements past the vanilla base count) VERBATIM — keeping infos in lockstep with names.
        if (base) {
            std::size_t baseObjs = 0;
            for (const auto& c : base->children) if (c.tag == "hkobject") ++baseObjs;
            std::size_t seen = 0;
            for (const auto& s : src.children) {
                if (s.tag != "hkobject") continue;
                if (seen++ < baseObjs) continue;   // skip the vanilla prefix; append only this mod's additions
                dst.children.push_back(s);
            }
        } else {  // no base node (e.g. a NEW node with no vanilla layer) — fall back to the by-value union
            for (const auto& s : src.children) {
                if (s.tag != "hkobject") continue;
                bool present = false;
                for (const auto& d : dst.children) if (d.tag == "hkobject" && deepEqual(d, s)) { present = true; break; }
                if (!present) dst.children.push_back(s);
            }
        }
    } else if (hasStr) {
        // <hkcstring> name arrays are a SET — dedup by value. A cross-mod duplicate name collapses to one,
        // and this is also what keeps animationNames rosters from padding (the OAR offset-underflow guard).
        for (const auto& s : src.children) {
            if (s.tag != "hkcstring") continue;
            bool present = false;
            for (const auto& d : dst.children) if (d.tag == "hkcstring" && deepEqual(d, s)) { present = true; break; }
            if (!present) dst.children.push_back(s);
        }
    } else {  // whitespace-token array (refs / scalars)
        auto dt = tokens(dst.text);
        for (const auto& t : tokens(src.text))
            if (std::find(dt.begin(), dt.end(), t) == dt.end()) dt.push_back(t);
        std::string joined;
        for (std::size_t i = 0; i < dt.size(); ++i) { if (i) joined += ' '; joined += dt[i]; }
        dst.text = joined;
    }
}
inline xml::Node* findParam(xml::Node& o, std::string_view name) {
    for (auto& c : o.children) if (c.tag == "hkparam" && c.attr("name") == name) return &c;
    return nullptr;
}

// One override layer: the mod's OPEN-applied node plus the set of top-level hkparam
// names it ACTUALLY changed (its MOD_CODE delta, from changedFields()). The merge is
// driven by that delta, not by diffing against vanilla — so a field a mod leaves
// outside MOD_CODE never overrides another mod's real change even when the mod's stale
// copy of it differs from true vanilla (the dual-wield enterNotifyEvents bug).
struct PatchLayer { xml::Node node; std::unordered_set<std::string> changed; };

// The set of top-level hkparam names a patch layer ACTUALLY changes, from its own MOD_CODE
// markers: diff the OPEN-applied node against the ORIGINAL-applied node. A field the mod leaves
// outside MOD_CODE is unchanged even if the mod's stale copy of it differs from true vanilla. A
// file with NO markers reports all its fields (the legacy full-override path; bashMerge's own
// changer filter — deepEqual vs base — still recovers the real edits). SHARED by the typed
// PatchConverter and the schema parseSourcesMerged so both build PatchLayer.changed identically —
// the one merge-input rule, no drift.
inline std::unordered_set<std::string> changedFields(const std::string& rawSrc) {
    auto params = [](const xml::Node& n) {
        std::vector<std::pair<std::string, const xml::Node*>> v;
        for (const auto& c : n.children) if (c.tag == "hkparam") v.emplace_back(std::string(c.attr("name")), &c);
        return v;
    };
    std::string openSrc = rawSrc; xml::StripPatchOriginals(openSrc);
    const xml::Node openN = xml::Parse(openSrc);
    std::unordered_set<std::string> out;
    if (rawSrc.find("MOD_CODE") == std::string::npos) {           // no markers -> all fields (legacy)
        for (auto& pr : params(openN)) out.insert(pr.first);
        return out;
    }
    std::string origSrc = rawSrc; xml::StripToOriginal(origSrc);
    const xml::Node origN = xml::Parse(origSrc);
    const auto on = params(openN), gn = params(origN);
    auto find = [](const std::vector<std::pair<std::string, const xml::Node*>>& v,
                   const std::string& k) -> const xml::Node* {
        for (auto& pr : v) if (pr.first == k) return pr.second;
        return nullptr;
    };
    for (auto& pr : on) { const xml::Node* g = find(gn, pr.first); if (!g || !deepEqual(*pr.second, *g)) out.insert(pr.first); }
    for (auto& pr : gn) if (!find(on, pr.first)) out.insert(pr.first);
    return out;
}

// A param-name -> is-guarded-positional-array predicate (empty = never guard, which is
// the converter's policy: union every multi-mod array exactly as before). The loader
// passes a real predicate so a positional-node-array collision reports instead of
// silently unioning.
using GuardPred = std::function<bool(const std::string&)>;
struct MergeReport { std::vector<std::string> guardErrors; };  // params refused by the guard

// A (class, field) -> is-this-a-`merge: compose` array predicate. The loader/converter binds it to
// SchemaRegistry::MergeTag so BOTH merge adapters read the SAME schema tag; empty = never compose
// (the legacy/typed path, count-based union exactly as before).
using ComposePred = std::function<bool(const std::string& cls, const std::string& field)>;

// Count an array param's <hkobject> item children.
inline std::size_t objCount(const xml::Node& p) {
    std::size_t n = 0; for (const auto& c : p.children) if (c.tag == "hkobject") ++n; return n;
}

// Compose an <hkobject>-array param onto `dst` (already a copy of `base`): element-wise over the base
// prefix — a slot ANY changer edited is REPLACED (last changer in load order wins), base slots no mod
// touched stay — then each changer's TAIL items (past base length) append in load order. The xml::Node
// mirror of the runtime YamlBehaviorLoader compose (BR-39): keeps transitions/states load-order-composed
// instead of unioning stale base entries. Caller guarantees every changer covers the base prefix.
inline void composeArrayInto(xml::Node& dst, const std::vector<const xml::Node*>& changers) {
    std::vector<xml::Node*> dstItems;                       // dst's item children (valid until we append)
    for (auto& c : dst.children) if (c.tag == "hkobject") dstItems.push_back(&c);
    std::vector<std::vector<const xml::Node*>> chItems(changers.size());
    for (std::size_t k = 0; k < changers.size(); ++k)
        for (const auto& c : changers[k]->children) if (c.tag == "hkobject") chItems[k].push_back(&c);
    const std::size_t baseN = dstItems.size();             // dst == base copy → base item count

    // loop 1 — element-wise over the base prefix: the LAST changer that edits slot i wins (compared
    // against the base value, which dstItems[i] still holds — we assign only after the inner loop).
    for (std::size_t i = 0; i < baseN; ++i) {
        const xml::Node* win = nullptr;
        for (std::size_t k = 0; k < changers.size(); ++k)
            if (i < chItems[k].size() && !deepEqual(*chItems[k][i], *dstItems[i])) win = chItems[k][i];
        if (win) *dstItems[i] = *win;
    }
    // loop 2 — append each changer's tail (items past the base length). dstItems is now stale (append
    // may realloc dst.children) but unused; chItems point into the const changer nodes, untouched.
    for (std::size_t k = 0; k < changers.size(); ++k)
        for (std::size_t i = baseN; i < chItems[k].size(); ++i)
            dst.children.push_back(*chItems[k][i]);
}

// Bashed merge: start from `base` (vanilla), apply each layer's real deltas via the
// shared `decideParam` rule. See decideParam for the per-param policy.
inline xml::Node bashMerge(const xml::Node& base, const std::vector<const PatchLayer*>& layers,
                           const GuardPred& guarded = {}, const ComposePred& compose = {},
                           MergeReport* report = nullptr) {
    xml::Node merged = base;
    const std::string cls(base.attr("class"));   // node class → schema `merge:` tag lookup (compose)

    // Candidate params = only those SOME layer actually changed, first-seen order.
    std::vector<std::string> names;
    for (const PatchLayer* L : layers)
        for (const auto& c : L->node.children)
            if (c.tag == "hkparam") {
                std::string n(c.attr("name"));
                if (L->changed.count(n) && std::find(names.begin(), names.end(), n) == names.end())
                    names.push_back(std::move(n));
            }

    for (const std::string& P : names) {
        const xml::Node* bp = nullptr;
        for (const auto& c : base.children) if (c.tag == "hkparam" && c.attr("name") == P) { bp = &c; break; }

        std::vector<const xml::Node*> changers;   // layers that actually changed P (and differ from base)
        bool isArr = false;
        for (const PatchLayer* L : layers) {
            if (!L->changed.count(P)) continue;   // P left outside this mod's MOD_CODE -> not its change
            const xml::Node* lp = nullptr;
            for (const auto& c : L->node.children) if (c.tag == "hkparam" && c.attr("name") == P) { lp = &c; break; }
            if (!lp) continue;
            if (isArrayParam(*lp)) isArr = true;
            if (!bp || !deepEqual(*lp, *bp)) changers.push_back(lp);
        }
        if (changers.empty()) continue;

        xml::Node* mp = findParam(merged, P);

        // compose (schema `merge: compose`): order-sensitive load-order composition — supersedes the
        // count-based union for tagged arrays (the xml mirror of the runtime). Needs a base array, 2+
        // changers, and every changer to COVER the base prefix (shorter = a removal that raw position-
        // alignment can't express); otherwise fall through to the count-based paths below.
        if (compose && !cls.empty() && isArr && changers.size() > 1 && bp && mp && compose(cls, P)) {
            const std::size_t baseN = objCount(*bp);
            bool editAppend = true;
            for (const xml::Node* ch : changers) if (objCount(*ch) < baseN) { editAppend = false; break; }
            if (editAppend) { composeArrayInto(*mp, changers); continue; }
        }

        const bool g = guarded && guarded(P);
        switch (decideParam(isArr, static_cast<int>(changers.size()), g)) {
            case ParamMerge::UnionArray:
                if (!mp) { merged.children.push_back(*changers.front()); }
                else for (const xml::Node* ch : changers) mergeArrayInto(*mp, *ch, bp);   // bp = vanilla base (tail-append infos)
                break;
            case ParamMerge::GuardError:
                if (report) report->guardErrors.push_back(P);
                break;   // refuse: leave base value untouched, never silently drop a mod's array
            case ParamMerge::ReplaceArray:
            case ParamMerge::LastWriter: {
                const xml::Node* win = changers.back();
                if (mp) { mp->text = win->text; mp->children = win->children; mp->attrs = win->attrs; }
                else merged.children.push_back(*win);
                break;
            }
            case ParamMerge::Keep:
                break;
        }
    }
    return merged;
}

}  // namespace havok::merge
