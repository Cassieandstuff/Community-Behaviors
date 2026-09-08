// YamlBehaviorLoader (Tier B, SECONDARY) implementation. Faithful C++ mirror of
// HKBuild\src\BehaviorReader.cs, retargeted onto ryml (rapidyaml) and the C++
// Def POCOs. This is the ONLY havok-core TU that includes ryml — keep it isolated.
//
// Wiring (matches Engine Relay): find_package(ryml CONFIG REQUIRED) +
// target_link_libraries(... ryml::ryml); includes below. Verified in the Visual
// Studio build, not in the standalone havok-core test build.

#include "havok/model/yaml/YamlBehaviorLoader.h"

#include "havok/model/BashMerge.h"   // shared merge decision (havok::merge::decideParam)
#include <havok-schema/HavokSchema.h> // SchemaRegistry — per-field `merge:` tag classifier
#include "havok/model/HavokEnums.h"  // enums::ResolveEnum for symbolic flag fields
#include "havok/sct/AnimDataFromBehavior.h"  // sct::ReadClipInputsFromClipsDir (implemented here — ryml is isolated to this TU)

// rapidyaml MUST come in via this shim (include/external/RymlInclude.h): it
// neutralizes a c4core v0.5.0 C++20 attribute bug that otherwise breaks the ryml
// headers in this C++23 TU. Same fix Engine Relay's ConfigLoader uses.
#include <RymlInclude.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace havok::model {

namespace fs = std::filesystem;

namespace {

// ── small text helpers (mirror BehaviorReader / ConfigLoader.Trim) ────────────
std::string trim(const std::string& s) {
    auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return {};
    auto e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

// Parse a YAML buffer in place, rethrowing any ryml error annotated with the
// source file so a bad file in a 100-file tree is identifiable (M-E hardening).
c4::yml::Tree parseNamed(std::string& text, const fs::path& p) {
    try {
        return c4::yml::parse_in_place(c4::to_substr(text));
    } catch (const std::exception& e) {
        throw std::runtime_error("YAML parse error in " + p.string() + ": " + e.what());
    }
}

bool contains(const std::string& hay, const std::string& needle) {
    return hay.find(needle) != std::string::npos;
}

// ── ryml scalar accessors ─────────────────────────────────────────────────────
bool hasChild(const c4::yml::ConstNodeRef& n, const char* key) {
    return n.readable() && n.is_map() && n.has_child(c4::to_csubstr(key));
}

std::string str(const c4::yml::ConstNodeRef& n, const char* key, const std::string& fb = "") {
    if (!hasChild(n, key)) return fb;
    auto c = n[c4::to_csubstr(key)];
    if (!c.has_val()) return fb;
    std::string out;
    c4::from_chars(c.val(), &out);
    return out;
}

// Map key for a decompiled node: its `id` when present (id-identity YAML), else
// its `name`. The id refactor keeps name resolution valid until Stage 5, so
// authored name-based YAML (True Flight / Engine Relay / Take to the Sky, which
// carry `name:` but no `id:`) must still key — and resolve — by name. References
// in such files are names too, so key and ref match; a decompiled file is all-id
// and never mixes the two.
std::string keyOf(const c4::yml::ConstNodeRef& n) {
    auto id = str(n, "id", "");
    return !id.empty() ? id : str(n, "name", "");
}

bool has(const c4::yml::ConstNodeRef& n, const char* key) { return hasChild(n, key); }

int toInt(const std::string& s, int fb = 0) {
    std::string t = trim(s);
    if (t.empty()) return fb;
    try { return std::stoi(t); } catch (...) { return fb; }
}

bool toBool(const std::string& s, bool fb = false) {
    std::string t = trim(s);
    if (t == "true" || t == "True" || t == "1") return true;
    if (t == "false" || t == "False" || t == "0") return false;
    return fb;
}

int intField(const c4::yml::ConstNodeRef& n, const char* key, int fb = 0) {
    if (!hasChild(n, key)) return fb;
    return toInt(str(n, key), fb);
}

bool boolField(const c4::yml::ConstNodeRef& n, const char* key, bool fb = false) {
    if (!hasChild(n, key)) return fb;
    return toBool(str(n, key), fb);
}

std::optional<std::string> optStr(const c4::yml::ConstNodeRef& n, const char* key) {
    if (!hasChild(n, key)) return std::nullopt;
    return str(n, key);
}

// ── bindings ──────────────────────────────────────────────────────────────────
std::optional<std::vector<BindingDef>> parseBindings(const c4::yml::ConstNodeRef& n) {
    if (!hasChild(n, "bindings")) return std::nullopt;
    auto node = n["bindings"];
    if (!node.is_seq()) return std::nullopt;
    std::vector<BindingDef> out;
    for (auto c : node) {
        BindingDef b;
        b.memberPath   = str(c, "memberPath");
        b.variableIndex = intField(c, "variableIndex", -1);
        b.variable     = optStr(c, "variable");
        b.bitIndex     = intField(c, "bitIndex", -1);
        b.bindingType  = str(c, "bindingType", "BINDING_TYPE_VARIABLE");
        b.enableTarget = boolField(c, "enableTarget", false);
        out.push_back(std::move(b));
    }
    if (out.empty()) return std::nullopt;
    return out;
}

// ── bone weights (named / raw / preset) ───────────────────────────────────────
std::optional<BoneWeightsDef> parseBoneWeights(const c4::yml::ConstNodeRef& n) {
    if (!hasChild(n, "boneWeights")) return std::nullopt;
    auto node = n["boneWeights"];
    BoneWeightsDef bw;
    bw.count = intField(node, "count", 0);
    bw.values = str(node, "values");
    if (hasChild(node, "preset")) bw.preset = str(node, "preset");
    if (hasChild(node, "bone_count")) bw.boneCount = intField(node, "bone_count", 0);
    if (hasChild(node, "default")) bw.defaultWeight = str(node, "default", "0.0");   // named-format fill for unlisted bones
    if (hasChild(node, "named")) {
        auto named = node["named"];
        if (named.is_map()) {
            std::map<std::string, std::string> m;
            for (auto kv : named) {
                std::string k, v;
                c4::from_chars(kv.key(), &k);
                if (kv.has_val()) c4::from_chars(kv.val(), &v);
                m[k] = v;
            }
            bw.named = std::move(m);
        }
    }
    return bw;
}

// ── inline event (id/event/payload) ───────────────────────────────────────────
InlineEventDef parseInlineEvent(const c4::yml::ConstNodeRef& n) {
    InlineEventDef e;
    e.id = intField(n, "id", -1);
    e.event = optStr(n, "event");
    e.payload = optStr(n, "payload");
    return e;
}

// ── transitions: parsed from the inline `transitions:` block ──────────────────
TransitionIntervalDef parseInterval(const c4::yml::ConstNodeRef& n) {
    TransitionIntervalDef iv;
    iv.enterEventId = intField(n, "enterEventId", -1);
    iv.enterEvent   = optStr(n, "enterEvent");
    iv.exitEventId  = intField(n, "exitEventId", -1);
    iv.exitEvent    = optStr(n, "exitEvent");
    iv.enterTime    = str(n, "enterTime", "0.000000");
    iv.exitTime     = str(n, "exitTime", "0.000000");
    return iv;
}

std::vector<TransitionInfoDef> parseTransitionsSeq(const c4::yml::ConstNodeRef& seq) {
    std::vector<TransitionInfoDef> out;
    if (!seq.readable() || !seq.is_seq()) return out;
    for (auto c : seq) {
        TransitionInfoDef t;
        if (hasChild(c, "triggerInterval"))  t.triggerInterval  = parseInterval(c["triggerInterval"]);
        if (hasChild(c, "initiateInterval")) t.initiateInterval = parseInterval(c["initiateInterval"]);
        t.transition       = str(c, "transition");
        t.condition        = optStr(c, "condition");
        t.conditionString  = optStr(c, "conditionString");
        t.eventId          = intField(c, "eventId", -1);
        t.event            = optStr(c, "event");
        t.toStateId        = intField(c, "toStateId", 0);
        t.toState          = optStr(c, "toState");
        t.fromNestedStateId = intField(c, "fromNestedStateId", 0);
        t.toNestedStateId   = intField(c, "toNestedStateId", 0);
        t.priority          = intField(c, "priority", 0);
        t.flags             = str(c, "flags", "FLAG_DISABLE_CONDITION");
        out.push_back(std::move(t));
    }
    return out;
}

// enter/exit notify events
std::optional<std::vector<EventPropertyDef>>
parseEventProps(const c4::yml::ConstNodeRef& n, const char* key) {
    if (!hasChild(n, key)) return std::nullopt;
    auto node = n[c4::to_csubstr(key)];
    if (!node.is_seq()) return std::nullopt;
    std::vector<EventPropertyDef> out;
    for (auto c : node) {
        EventPropertyDef e;
        e.id      = intField(c, "id", -1);
        e.event   = optStr(c, "event");
        e.payload = str(c, "payload", "null");
        out.push_back(std::move(e));
    }
    if (out.empty()) return std::nullopt;
    return out;
}

// triggers on clips
std::optional<std::vector<ClipTriggerDef>> parseTriggers(const c4::yml::ConstNodeRef& n) {
    if (!hasChild(n, "triggers")) return std::nullopt;
    auto node = n["triggers"];
    if (!node.is_seq()) return std::nullopt;
    std::vector<ClipTriggerDef> out;
    for (auto c : node) {
        ClipTriggerDef t;
        t.localTime = str(c, "localTime", "0.000000");
        t.eventId   = intField(c, "eventId", -1);
        t.event     = optStr(c, "event");
        t.payload   = str(c, "payload", "null");
        t.relativeToEndOfClip = boolField(c, "relativeToEndOfClip", false);
        t.acyclic             = boolField(c, "acyclic", false);
        t.isAnnotation        = boolField(c, "isAnnotation", false);
        out.push_back(std::move(t));
    }
    if (out.empty()) return std::nullopt;
    return out;
}

// generic-modifier extra params: every scalar key beyond the base fields.
std::vector<GenericParam> parseGenericExtraParams(const c4::yml::ConstNodeRef& root) {
    static const std::vector<std::string> baseFields = {"class", "name", "userData", "enable", "bindings"};
    std::vector<GenericParam> out;
    if (!root.is_map()) return out;
    for (auto c : root) {
        std::string key;
        c4::from_chars(c.key(), &key);
        if (std::find(baseFields.begin(), baseFields.end(), key) != baseFields.end()) continue;

        GenericParam p;
        p.name = key;
        if (c.is_seq()) {
            // ref list or inline-object list
            bool inlineObj = false;
            for (auto e : c) { if (e.is_map()) { inlineObj = true; break; } }
            if (inlineObj) {
                p.kind = GenericParamKind::InlineObjectList;
                std::vector<GenericInlineObjectEntry> entries;
                for (auto e : c) {
                    GenericInlineObjectEntry entry;
                    if (e.is_map())
                        for (auto f : e) {
                            std::string fk, fv;
                            c4::from_chars(f.key(), &fk);
                            if (f.has_val()) c4::from_chars(f.val(), &fv);
                            entry.fields.emplace_back(fk, fv);
                        }
                    entries.push_back(std::move(entry));
                }
                p.inlineObjectListValue = std::move(entries);
            } else {
                p.kind = GenericParamKind::RefList;
                std::vector<std::string> refs;
                for (auto e : c) {
                    if (!e.has_val()) continue;
                    std::string v; c4::from_chars(e.val(), &v);
                    refs.push_back(trim(v));
                }
                p.refListValue = std::move(refs);
            }
        } else if (c.is_map()) {
            // inline event (event/id + optional payload)
            p.kind = GenericParamKind::InlineEvent;
            p.eventValue = parseInlineEvent(c);
        } else if (c.has_val()) {
            p.kind = GenericParamKind::Scalar;
            std::string v; c4::from_chars(c.val(), &v);
            p.scalarValue = trim(v);
        }
        out.push_back(std::move(p));
    }
    return out;
}

// peek the `class:` scalar of a YAML doc without full typed parse.
std::string peekClass(const c4::yml::ConstNodeRef& root) {
    return str(root, "class");
}

// ── scanSourceSection: the ONE node-collection scan (Stage 1 consolidation) ─────────────────────
// Every yaml node file under `sub` in ONE layer source. For each: parse, skip keyless nodes, and hand
// the callback (class, key, moved-text). This is the single place that reads + identifies a unit's
// node files — both the graph loader (eachYaml, which then groups+merges+builds) and NodeContributions
// (which accumulates per-layer overlaps) go through it, so the two can no longer drift (they were
// hand-synced with a "MUST mirror" comment). Per-SOURCE (not the whole unit) so each caller keeps its
// own layer/section loop order — behavior stays identical to the two inlined scans this replaces.
template <class Cb>
void scanSourceSection(const IUnitSource& src, const char* sub, bool recursive, Cb&& cb) {
    for (const std::string& rel : src.listYaml(sub, recursive)) {
        auto text = src.read(rel);
        if (!text) continue;
        std::string   probe = *text;                       // parseNamed mutates in place; probe for identity
        c4::yml::Tree tree  = parseNamed(probe, fs::path(rel));
        std::string   k     = keyOf(tree.rootref());
        if (k.empty()) continue;                            // keyless: both consumers skip it
        std::string   cls   = peekClass(tree.rootref());
        cb(std::move(cls), std::move(k), std::move(*text));
    }
}

// ── skeleton.yaml: read `character assets/skeleton.yaml` from the source ───────
// (DiskUnitSource resolves the vanilla upward-search convention internally; an
// in-memory source packages it unit-relative.)
std::vector<std::string> findAndLoadSkeleton(const IUnitSource& src) {
    auto text = src.read("character assets/skeleton.yaml");
    if (!text) return {};
    c4::yml::Tree tree = c4::yml::parse_in_place(c4::to_substr(*text));
    auto root = tree.rootref();
    std::vector<std::string> bones;
    if (root.readable() && root.has_child("bones")) {
        auto node = root["bones"];
        if (node.is_seq())
            for (auto c : node) {
                if (!c.has_val()) continue;
                std::string b; c4::from_chars(c.val(), &b);
                bones.push_back(b);
            }
    }
    return bones;
}

// ── native runtime merge (step 1): bash-merge delta layers onto a base node ──────
// Shares the union/last-writer DECISION with the offline converter via
// havok::merge::decideParam (BashMerge.h); the mechanics here are ryml-native (the xml
// mechanics live in PatchConverter). A delta carries only the params the mod changed,
// plus an optional top-level `bash: merge|replace` (merge = field/array bash, the
// default; replace = wholesale). A change to a base value the delta repeats verbatim is
// a no-op, so a full node re-stated as a "delta" merges to itself.

// Optional consumer-supplied sink for NON-FATAL merge notices (today: same-slot
// collisions on a positional array, where load-order last-writer drops one mod's
// edit). havok-core has no logger by design and reserves `throw` for FATAL merge
// refusals (the guard), so a non-fatal notice routes through a sink the consumer
// opts into (BR -> its log; the CLI -> stderr) via YamlBehaviorLoader::
// SetDiagnosticSink. Unset (tests, default) = quiet; the merge result is unchanged.
std::function<void(const std::string&)> g_mergeDiag;
void emitMergeDiag(const std::string& m) { if (g_mergeDiag) g_mergeDiag(m); }

// Ambient schema for the merge classifier (SetSchemaRegistry). When set, a field's merge
// policy is read from its `merge:` tag in the Havok/ descriptor — the schema owns the
// decision, and the converter's merge reads the SAME tag, so they can't drift. Null (default)
// = fall back to the built-in compose/guarded name sets (pre-schema-tag behaviour).
const schema::SchemaRegistry* g_mergeSchema = nullptr;
bool g_mergeStrict = false;   // gate-only: disable the name-set fallback so the tag alone drives merge

// The `merge:` tag declared for field `field` on class `cls` (e.g. "compose"/"guarded"), or
// "" when no schema is set, the class/field is unknown, or the field carries no tag. Only the
// class's OWN fields are consulted — the compose/guarded arrays (transitions/states) are direct
// members of their class, so no parent-chain walk is needed here.
std::string mergeTagFor(const std::string& cls, const std::string& field) {
    if (!g_mergeSchema || cls.empty()) return {};
    return g_mergeSchema->MergeTag(cls, field);   // the shared query — same answer the converter gets
}

// Structural equality of two ryml nodes (scalar val, or children compared positionally
// with their keys). Mirrors havok::merge::deepEqual on the YAML shape.
bool rymlEqual(const c4::yml::Tree& ta, c4::yml::id_type a, const c4::yml::Tree& tb, c4::yml::id_type b) {
    const bool av = ta.has_val(a), bv = tb.has_val(b);
    if (av != bv) return false;
    if (av) return ta.val(a) == tb.val(b);                 // scalar leaf
    c4::yml::id_type ca = ta.first_child(a), cb = tb.first_child(b);
    for (; ca != c4::yml::NONE && cb != c4::yml::NONE;
           ca = ta.next_sibling(ca), cb = tb.next_sibling(cb)) {
        const bool ak = ta.has_key(ca), bk = tb.has_key(cb);
        if (ak != bk) return false;
        if (ak && ta.key(ca) != tb.key(cb)) return false;
        if (!rymlEqual(ta, ca, tb, cb)) return false;
    }
    return ca == c4::yml::NONE && cb == c4::yml::NONE;      // same child count
}

// STRICTLY-guarded positional node arrays: a SEPARATE field stores a POSITIONAL INDEX into
// the array — `indexOfSyncMasterChild` into a state machine's `children`,
// `selectedGeneratorIndex` into a selector's `generators` — so a reorder/removal/mid-insert
// silently repoints that index at the wrong slot. A multi-mod non-append edit therefore stays
// guarded (throws) unless every mod is a pure APPEND (indices preserved; see isPureAppend).
//
// `modifiers` is DELIBERATELY NOT guarded: an hkbModifierList's elements are node-id refs and
// NOTHING stores a positional index INTO the array — only EXECUTION ORDER matters. So two mods
// each injecting a modifier into the same list (e.g. TK Dodge + DMCO both adding to
// PlayerBowModList) union safely by node-id identity (base order kept, additions appended) via
// the UnionArray path — where the old guard threw and failed the whole graph -> dodge A-pose
// (BR user report 2026-08-28). The only compromise is a mod's intended mid-insert POSITION
// becomes an end-append; for independent (enable-gated) modifiers that's functionally identical
// and vastly better than refusing the merge. The union path emits a diag so it's never silent.
bool isGuardedArray(const std::string& p) {
    return p == "children" || p == "generators";
}

// A mod's array is a pure APPEND onto the base when the base's items are a positional
// PREFIX of the mod's (same items, same order; the mod only adds at the end). Two mods
// each appending to a positional array union safely — base slots keep their indices,
// additions accumulate last. A mod that drops/reorders/edits a base item is NOT a prefix.
bool isPureAppend(const c4::yml::Tree& bt, c4::yml::id_type ba,
                  const c4::yml::Tree& mt2, c4::yml::id_type ma) {
    if (ba == c4::yml::NONE) return true;               // base had no array -> all additions append
    c4::yml::id_type bc = bt.first_child(ba), mc = mt2.first_child(ma);
    for (; bc != c4::yml::NONE; bc = bt.next_sibling(bc), mc = mt2.next_sibling(mc)) {
        if (mc == c4::yml::NONE) return false;           // mod dropped a base item
        if (!rymlEqual(bt, bc, mt2, mc)) return false;   // mod reordered / edited a base item
    }
    return true;                                         // base is a prefix; mod may append past it
}

// A mod's array is a SAME-LENGTH edit of the base when it has the SAME child count and at
// least one slot differs (no add/remove; every index still maps to the same position). Two
// mods each editing DIFFERENT slots of a same-length positional array can merge ELEMENT-WISE
// (per-index base-relative last-writer) without shifting any index ref (selectedGeneratorIndex
// / indexOfSyncMasterChild stay valid). Requires a base array (a from-nothing add is an append,
// not an in-place edit).
bool sameLengthEdit(const c4::yml::Tree& bt, c4::yml::id_type ba,
                    const c4::yml::Tree& mt2, c4::yml::id_type ma) {
    if (ba == c4::yml::NONE) return false;               // no base array -> not an in-place edit
    c4::yml::id_type bc = bt.first_child(ba), mc = mt2.first_child(ma);
    bool differs = false;
    for (; bc != c4::yml::NONE && mc != c4::yml::NONE;
           bc = bt.next_sibling(bc), mc = mt2.next_sibling(mc))
        if (!rymlEqual(bt, bc, mt2, mc)) differs = true;
    return bc == c4::yml::NONE && mc == c4::yml::NONE && differs;   // equal length, ≥1 slot changed
}

// A "compose" (order-sensitive, load-order-composed) array — transitions / states. NOT
// positionally index-guarded (nothing stores an index INTO it), but a mod's edit of a base slot
// REPLACES that slot; base slots no mod touched are kept; each mod's new entries append. This is
// the rule verified byte-for-byte against Pandora on the combat states (AttackState/BlockState);
// the decideParam UnionArray path (base ∪ every mod, dedup-exact) instead KEEPS the stale base
// slot a mod meant to replace → the attack-commitment loss + shield-drop bug.
// [Step 3(b) landed: the schema `merge: compose` tag (composeArray, below) is now the primary
//  classifier; this name set is the FALLBACK for when no schema registry is wired.]
bool isComposeArray(const std::string& p) {
    return p == "transitions" || p == "states";
}

// Schema-aware classifiers: a field's `merge:` tag wins when it carries one. Otherwise the choice
// is between the built-in name set (NON-strict — the safe production fallback) and returning "not
// this policy" (STRICT gate mode — the fallback is disabled, so the schema tag alone drives the
// merge). In strict mode a real composable array whose tag is missing then UNIONS instead of
// composing, diverging from the ground truth, which the byte-diff gate catches — that's what makes
// the gate un-foolable without a name-based check that would false-positive on same-named pointer
// fields (e.g. hkbStateMachineStateInfo.transitions, a ptr the `&& isArr` site already excludes).
// An explicit tag is authoritative even when it names a DIFFERENT policy. `cls` = node class (peekClass).
bool composeArray(const std::string& cls, const std::string& p) {
    if (const std::string t = mergeTagFor(cls, p); !t.empty()) return t == "compose";
    if (g_mergeSchema && g_mergeStrict) return false;   // strict: tag must carry it; no fallback
    return isComposeArray(p);
}
bool guardedArray(const std::string& cls, const std::string& p) {
    if (const std::string t = mergeTagFor(cls, p); !t.empty()) return t == "guarded";
    if (g_mergeSchema && g_mergeStrict) return false;   // strict: tag must carry it; no fallback
    return isGuardedArray(p);
}

// A changer qualifies for compose when it covers the whole base as a positional PREFIX — it may
// edit base slots in place and append past them, but must not be SHORTER than base (a removal,
// which raw position-alignment can't express). Reorders inside the prefix aren't detected here;
// the verified combat states are clean edit+append, and removal/reorder is a documented Phase-1
// residual, not the main line — such a changer falls through to the existing decideParam paths.
bool isEditPlusAppend(const c4::yml::Tree& bt, c4::yml::id_type ba,
                      const c4::yml::Tree& mt2, c4::yml::id_type ma) {
    if (ba == c4::yml::NONE) return true;               // no base array -> all additions append
    c4::yml::id_type bc = bt.first_child(ba), mc = mt2.first_child(ma);
    for (; bc != c4::yml::NONE; bc = bt.next_sibling(bc), mc = mt2.next_sibling(mc))
        if (mc == c4::yml::NONE) return false;          // changer shorter than base = removal
    return true;                                        // changer covers the base prefix (may append)
}

// Bash-merge `deltas` (load order, all bash:merge) onto `mt` (the effective base,
// mutated in place). `nodeKey` is only for the guard's error message.
void mergeLayers(c4::yml::Tree& mt, const std::vector<c4::yml::Tree*>& deltas,
                 const std::string& nodeKey) {
    const c4::yml::id_type mroot = mt.root_id();
    const std::string cls = peekClass(mt.rootref());   // node class → schema `merge:` tag lookup
    // candidate params = every top-level key some delta carries (first-seen), minus `bash`
    std::vector<std::string> cand;
    for (const c4::yml::Tree* dt : deltas)
        for (c4::yml::id_type c = dt->first_child(dt->root_id()); c != c4::yml::NONE; c = dt->next_sibling(c)) {
            if (!dt->has_key(c) || dt->key(c) == "bash") continue;
            std::string k(dt->key(c).str, dt->key(c).len);
            if (std::find(cand.begin(), cand.end(), k) == cand.end()) cand.push_back(std::move(k));
        }
    for (const std::string& P : cand) {
        const c4::csubstr Pc = c4::to_csubstr(P);
        const c4::yml::id_type bp = mt.find_child(mroot, Pc);   // base value (NONE if absent)
        std::vector<c4::yml::Tree*> changers;
        bool isArr = false;
        for (c4::yml::Tree* dt : deltas) {
            const c4::yml::id_type dp = dt->find_child(dt->root_id(), Pc);
            if (dp == c4::yml::NONE) continue;
            if (dt->is_seq(dp)) isArr = true;
            if (bp == c4::yml::NONE || !rymlEqual(mt, bp, *dt, dp)) changers.push_back(dt);
        }
        if (changers.empty()) continue;
        // COMPOSE (transitions / states) touched by 2+ mods. Walk the base positions element-wise
        // (a slot any changer edited is replaced; last changer in load order wins), then append
        // each changer's tail (entries past the base length). Pandora-parity; the UnionArray path
        // below would keep the stale base slot a mod meant to replace. A single changer is already
        // correct via ReplaceArray (its whole array == base-with-its-edits + its appends), so only
        // the 2+ case is intercepted. A changer that REMOVES base entries falls through (isEditPlusAppend).
        // NB the `bp != NONE` guard: the base must actually HAVE the array (bp, == mp below). Without
        // it, isEditPlusAppend's ba==NONE early-out lets control enter with a NONE mp, and the
        // first_child/duplicate calls then deref a wild node. Mirrors the xml merge's `bp && mp` guard
        // (BashMerge.h) — the two merge directions must stay lockstep. A base lacking the array falls
        // through to the count-based paths (append via UnionArray) instead.
        if (composeArray(cls, P) && isArr && changers.size() > 1 && bp != c4::yml::NONE) {
            bool editAppend = true;
            for (c4::yml::Tree* dt : changers)
                if (!isEditPlusAppend(mt, bp, *dt, dt->find_child(dt->root_id(), Pc))) { editAppend = false; break; }
            if (editAppend) {
                const c4::yml::id_type mp = mt.find_child(mroot, Pc);   // base array (== bp, present)
                // per-changer cursor into its array, advanced in lockstep with the base positions
                std::vector<std::pair<c4::yml::Tree*, c4::yml::id_type>> cur;
                cur.reserve(changers.size());
                for (c4::yml::Tree* dt : changers)
                    cur.emplace_back(dt, dt->first_child(dt->find_child(dt->root_id(), Pc)));
                // loop 1 — element-wise over the base prefix: last changer whose slot differs wins
                c4::yml::id_type base_i = mt.first_child(mp);
                while (base_i != c4::yml::NONE) {
                    const c4::yml::id_type next = mt.next_sibling(base_i);
                    c4::yml::Tree* win = nullptr; c4::yml::id_type winSlot = c4::yml::NONE;
                    for (auto& c : cur)
                        if (c.second != c4::yml::NONE && !rymlEqual(mt, base_i, *c.first, c.second)) {
                            win = c.first; winSlot = c.second;
                        }
                    if (win != nullptr) {
                        mt.duplicate(win, winSlot, mp, base_i);   // insert winner after the base slot
                        mt.remove(base_i);                         // drop base slot -> in-place replace
                    }
                    for (auto& c : cur)
                        if (c.second != c4::yml::NONE) c.second = c.first->next_sibling(c.second);
                    base_i = next;
                }
                // loop 2 — append each changer's tail (entries past the base length), in load order
                for (auto& c : cur)
                    for (c4::yml::id_type t = c.second; t != c4::yml::NONE; t = c.first->next_sibling(t))
                        mt.duplicate(c.first, t, mp, mt.last_child(mp));
                continue;   // fully composed; skip the decideParam switch
            }
        }
        // Guarded (positional) array touched by 2+ mods: auto-union is safe only if every
        // mod's array is a pure append onto the base (indices preserved). Otherwise keep
        // the loud guard — a reorder/removal can't be auto-merged without corrupting refs.
        bool guarded = guardedArray(cls, P);
        if (guarded && isArr && changers.size() > 1) {
            bool allAppend = true;
            for (c4::yml::Tree* dt : changers)
                if (!isPureAppend(mt, bp, *dt, dt->find_child(dt->root_id(), Pc))) { allAppend = false; break; }
            if (allAppend) guarded = false;   // pure appends -> union preserves positional indices
        }
        // Guarded array, 2+ mods, NOT all pure-append: if every mod is a SAME-LENGTH edit
        // (positions preserved, only slot values change), merge ELEMENT-WISE base-relative —
        // each slot goes to the highest-priority mod that changed it vs the vanilla base
        // (Pandora chunk-merge parity). This composes "mod A edits slot 6, mod B edits slot 3"
        // that ReplaceArray (keeps only the last mod's whole array) and GuardError (refuses)
        // can't. A length change (removal, or a non-prefix grow = mid-insert/reorder) is NOT a
        // same-length edit, so it falls through to the guard below and still refuses loudly.
        if (guarded && isArr && changers.size() > 1) {
            bool allSameLen = true;
            for (c4::yml::Tree* dt : changers)
                if (!sameLengthEdit(mt, bp, *dt, dt->find_child(dt->root_id(), Pc))) { allSameLen = false; break; }
            if (allSameLen) {
                const c4::yml::id_type mp = mt.find_child(mroot, Pc);   // base array (== bp, present)
                // per-changer cursor into its (same-length) array, advanced in lockstep w/ base
                std::vector<std::pair<c4::yml::Tree*, c4::yml::id_type>> cur;
                cur.reserve(changers.size());
                for (c4::yml::Tree* dt : changers)
                    cur.emplace_back(dt, dt->first_child(dt->find_child(dt->root_id(), Pc)));
                c4::yml::id_type base_i = mt.first_child(mp);
                for (int idx = 0; base_i != c4::yml::NONE; ++idx) {
                    const c4::yml::id_type next = mt.next_sibling(base_i);
                    // Highest-priority changer whose slot idx differs from the vanilla base slot
                    // (deltas are in ascending priority, so the LAST differing one wins).
                    c4::yml::Tree* win = nullptr; c4::yml::id_type winSlot = c4::yml::NONE;
                    for (auto& c : cur)
                        if (c.second != c4::yml::NONE && !rymlEqual(mt, base_i, *c.first, c.second)) {
                            win = c.first; winSlot = c.second;
                        }
                    if (win != nullptr) {
                        // Same-slot collision = another changer ALSO edits this slot to a value
                        // that differs from the winner's. Last-writer already chose; surface the
                        // dropped edit (never silent) — offline is deterministic, in-game BR logs it.
                        int dropped = 0;
                        for (auto& c : cur)
                            if (c.second != c4::yml::NONE && !rymlEqual(mt, base_i, *c.first, c.second) &&
                                !rymlEqual(*win, winSlot, *c.first, c.second)) ++dropped;
                        if (dropped > 0)
                            emitMergeDiag("BR merge: same-slot collision on positional array '" + P +
                                          "'[" + std::to_string(idx) + "] of node '" + nodeKey + "' — " +
                                          std::to_string(dropped + 1) +
                                          " mods edit it to differing values; highest-priority wins.");
                        mt.duplicate(win, winSlot, mp, base_i);   // insert winner immediately after base
                        mt.remove(base_i);                         // drop base slot -> in-place replace
                    }
                    for (auto& c : cur)
                        if (c.second != c4::yml::NONE) c.second = c.first->next_sibling(c.second);
                    base_i = next;
                }
                continue;   // param fully merged element-wise; skip the decideParam switch
            }
        }
        switch (havok::merge::decideParam(isArr, static_cast<int>(changers.size()), guarded)) {
            case havok::merge::ParamMerge::GuardError:
                throw std::runtime_error(
                    "YamlBehaviorLoader: bash-merge guard — " + std::to_string(changers.size()) +
                    " mods make NON-APPEND changes to positional array '" + P + "' on node '" + nodeKey +
                    "' (a reorder/removal/mid-array edit); refusing to auto-union (index refs would "
                    "corrupt). Pre-merge offline or express one as an override.");
            case havok::merge::ParamMerge::UnionArray: {
                // A modifier list unioned across 2+ mods (the ex-guard case: node-id refs, no
                // positional index into the array). Never silent — surface the order compromise.
                if (P == "modifiers")
                    emitMergeDiag("BR merge: unioned " + std::to_string(changers.size()) +
                                  " mods' edits to modifier list on node '" + nodeKey +
                                  "' by node-id identity (execution order: base, then additions).");
                c4::yml::id_type mp = mt.find_child(mroot, Pc);
                if (mp == c4::yml::NONE) {                       // base lacked it: seed from first changer
                    c4::yml::Tree* f = changers.front();
                    mt.duplicate(f, f->find_child(f->root_id(), Pc), mroot, mt.last_child(mroot));
                    mp = mt.find_child(mroot, Pc);
                }
                for (c4::yml::Tree* dt : changers) {
                    const c4::yml::id_type dp = dt->find_child(dt->root_id(), Pc);
                    for (c4::yml::id_type it = dt->first_child(dp); it != c4::yml::NONE; it = dt->next_sibling(it)) {
                        bool present = false;
                        for (c4::yml::id_type m = mt.first_child(mp); m != c4::yml::NONE; m = mt.next_sibling(m))
                            if (rymlEqual(mt, m, *dt, it)) { present = true; break; }
                        if (!present) mt.duplicate(dt, it, mp, mt.last_child(mp));
                    }
                }
                break;
            }
            case havok::merge::ParamMerge::ReplaceArray:
            case havok::merge::ParamMerge::LastWriter: {
                c4::yml::Tree* win = changers.back();
                const c4::yml::id_type wp = win->find_child(win->root_id(), Pc);
                const c4::yml::id_type mp = mt.find_child(mroot, Pc);
                if (mp != c4::yml::NONE) mt.remove(mp);
                mt.duplicate(win, wp, mroot, mt.last_child(mroot));
                break;
            }
            case havok::merge::ParamMerge::Keep:
                break;
        }
    }
}

} // namespace

// Load one behavior tree's nodes INTO `data`, overwriting same-named entries. The
// loader keys every map by name, so loading a base then delta dirs in order yields
// the merged graph for free (the resolver's record-level merge). `requireRoot`
// throws on a missing behavior.yaml — the base needs it; a merged-in delta may omit
// it (then it keeps the base's root/rootGenerator).
static void loadDirInto(BehaviorData& data,
                        const std::vector<std::shared_ptr<const IUnitSource>>& sources,
                        bool requireRoot) {
    // ── behavior.yaml (per layer, in load order; only the base is required) ──
    for (std::size_t li = 0; li < sources.size(); ++li) {
        std::optional<std::string> text = sources[li]->read("behavior.yaml");
        if (!text || text->empty()) {
            if (requireRoot && li == 0)
                throw std::runtime_error("YamlBehaviorLoader: missing behavior.yaml in base unit");
            continue;
        }
        c4::yml::Tree tree = c4::yml::parse_in_place(c4::to_substr(*text));
        auto root = tree.rootref();
        if (hasChild(root, "packfile")) {
            auto pf = root["packfile"];
            data.behavior.packfile.classVersion    = intField(pf, "classversion", 8);
            data.behavior.packfile.contentsVersion  = str(pf, "contentsversion", "hk_2010.2.0-r1");
        }
        if (hasChild(root, "behavior")) {
            auto b = root["behavior"];
            data.behavior.behavior.name         = str(b, "name");
            data.behavior.behavior.variableMode = str(b, "variableMode", "VARIABLE_MODE_DISCARD_WHEN_INACTIVE");
            data.behavior.behavior.rootGenerator = str(b, "rootGenerator");
            data.behavior.behavior.data         = optStr(b, "data").value_or("null");
        }
    }

    // Merge seam: gather each node file across ALL layers keyed by keyOf (id-else-name),
    // in first-seen (load) order, then hand the section body ONE root per key. Today
    // (step 0b) a key that recurs in a later layer is last-writer — identical to the old
    // per-layer parse+assign. Step 1 replaces the pick-last (`layers.back()`) with the
    // shared bash-merge (havok::merge::decideParam) over all the layers' raw trees.
    auto eachYaml = [&](const char* sub, bool recursive, auto&& fn) {
        std::vector<std::string> order;                                    // group keys, first-seen order
        std::unordered_map<std::string, std::vector<std::string>> byKey;   // group key -> per-layer texts
        std::unordered_map<std::string, std::string> keyName;              // group key -> keyOf (merge diag)
        // Group by (class,key), NOT key alone. Within one section dir two files can legitimately
        // share a name when their `class:` differs — a state 'X' and the nested state machine 'X' it
        // wraps (Engine Relay's Shd_BlockIdle_1stP) go to different maps (data.states vs
        // data.stateMachines) and must NOT merge. A real cross-layer delta repeats the base's class
        // (the converter emits every node via the per-class decompiler), so same-object layers still
        // group and bash-merge. Scan via the shared scanUnitNodes (Stage 1) — the single collector.
        for (const auto& src : sources) {
            if (!src) continue;
            scanSourceSection(*src, sub, recursive,
                [&](std::string cls, std::string k, std::string text) {
                    std::string gk = std::move(cls);
                    gk += '\x1f';
                    gk += k;
                    auto it = byKey.find(gk);
                    if (it == byKey.end()) { order.push_back(gk); keyName.emplace(gk, k); }
                    byKey[gk].push_back(std::move(text));
                });
        }
        for (const std::string& gk : order) {
            std::vector<std::string>& texts = byKey[gk];
            if (texts.size() == 1) {                           // single layer: no merge
                std::string buf = texts[0];
                c4::yml::Tree tree = c4::yml::parse_in_place(c4::to_substr(buf));
                fn(tree.rootref());
                continue;
            }
            // Multi-layer: bash-merge. Effective base = the last `bash: replace` layer
            // (else layer 0); deltas = the (bash:merge) layers after it. Keep every
            // buffer alive: parse_in_place references it and merged nodes may too.
            std::vector<std::string> bufs;
            bufs.reserve(texts.size());
            for (const std::string& t : texts) bufs.push_back(t);
            std::vector<c4::yml::Tree> trees(bufs.size());
            for (std::size_t i = 0; i < bufs.size(); ++i)
                trees[i] = c4::yml::parse_in_place(c4::to_substr(bufs[i]));
            std::size_t baseIdx = 0;
            for (std::size_t i = 1; i < trees.size(); ++i)
                if (str(trees[i].rootref(), "bash", "merge") == "replace") baseIdx = i;
            std::vector<c4::yml::Tree*> deltas;
            for (std::size_t i = baseIdx + 1; i < trees.size(); ++i) deltas.push_back(&trees[i]);
            mergeLayers(trees[baseIdx], deltas, keyName[gk]);
            fn(trees[baseIdx].rootref());
        }
    };

    // ── clips/ ──
    eachYaml("clips", true, [&](const c4::yml::ConstNodeRef& r) {
        ClipGeneratorDef c;
        c.name          = str(r, "name");
        c.animationName = str(r, "animationName");
        c.mode          = str(r, "mode", "MODE_SINGLE_PLAY");
        c.playbackSpeed = str(r, "playbackSpeed", "1.000000");
        c.cropStartAmountLocalTime = str(r, "cropStartAmountLocalTime", "0.000000");
        c.cropEndAmountLocalTime   = str(r, "cropEndAmountLocalTime", "0.000000");
        c.startTime                = str(r, "startTime", "0.000000");
        c.enforcedDuration         = str(r, "enforcedDuration", "0.000000");
        c.userControlledTimeFraction = str(r, "userControlledTimeFraction", "0.000000");
        c.animationBindingIndex    = intField(r, "animationBindingIndex", -1);
        c.flags                    = static_cast<int>(enums::ResolveEnum(str(r, "flags", "0"), enums::ClipGeneratorFlags()));
        c.userData                 = intField(r, "userData", 0);
        c.triggers                 = parseTriggers(r);
        c.bindings                 = parseBindings(r);   // clips bind e.g. playbackSpeed -> a variable
        if (auto _k = keyOf(r); !_k.empty()) data.clips[_k] = std::move(c);
    });

    // ── selectors/ ──
    eachYaml("selectors", true, [&](const c4::yml::ConstNodeRef& r) {
        ManualSelectorDef s;
        s.name = str(r, "name");
        s.selectedGeneratorIndex = intField(r, "selectedGeneratorIndex", 0);
        s.currentGeneratorIndex  = intField(r, "currentGeneratorIndex", 0);
        s.userData = intField(r, "userData", 0);
        s.bindings = parseBindings(r);
        if (hasChild(r, "generators") && r["generators"].is_seq())
            for (auto g : r["generators"]) {
                if (!g.has_val()) continue;
                std::string v; c4::from_chars(g.val(), &v); s.generators.push_back(trim(v));
            }
        if (auto _k = keyOf(r); !_k.empty()) data.selectors[_k] = std::move(s);
    });

    // ── transitions/ ──
    eachYaml("transitions", true, [&](const c4::yml::ConstNodeRef& r) {
        TransitionEffectDef t;
        t.name = str(r, "name");
        t.userData = intField(r, "userData", 0);
        t.selfTransitionMode = str(r, "selfTransitionMode", "SELF_TRANSITION_MODE_CONTINUE_IF_CYCLIC_BLEND_IF_ACYCLIC");
        t.eventMode = str(r, "eventMode", "EVENT_MODE_DEFAULT");
        t.duration  = str(r, "duration", "0.200000");
        t.toGeneratorStartTimeFraction = str(r, "toGeneratorStartTimeFraction", "0.000000");
        t.flags     = str(r, "flags", "0");
        t.endMode   = str(r, "endMode", "END_MODE_NONE");
        t.blendCurve = str(r, "blendCurve", "BLEND_CURVE_SMOOTH");
        t.applySelfTransition     = boolField(r, "applySelfTransition", false);
        t.initializeCharacterPose = boolField(r, "initializeCharacterPose", false);
        t.bindings  = parseBindings(r);
        if (auto _k = keyOf(r); !_k.empty()) data.transitionEffects[_k] = std::move(t);
    });

    // ── generators/ (class-disambiguated) ──
    eachYaml("generators", true, [&](const c4::yml::ConstNodeRef& r) {
        std::string cls = peekClass(r);

        if (cls == "BSCyclicBlendTransitionGenerator") {
            BSCyclicBlendTransitionGeneratorDef cb;
            cb.name = str(r, "name");
            cb.userData = intField(r, "userData", 0);
            cb.pBlenderGenerator = str(r, "pBlenderGenerator");
            if (hasChild(r, "EventToFreezeBlendValue")) cb.eventToFreezeBlendValue = parseInlineEvent(r["EventToFreezeBlendValue"]);
            if (hasChild(r, "EventToCrossBlend"))        cb.eventToCrossBlend       = parseInlineEvent(r["EventToCrossBlend"]);
            cb.fBlendParameter     = str(r, "fBlendParameter", "0.000000");
            cb.fTransitionDuration = str(r, "fTransitionDuration", "0.200000");
            cb.eBlendCurve         = str(r, "eBlendCurve", "BLEND_CURVE_SMOOTH");
            cb.bindings = parseBindings(r);
            if (auto _k = keyOf(r); !_k.empty()) data.cyclicBlendGenerators[_k] = std::move(cb);
        } else if (cls == "BSBoneSwitchGenerator") {
            BSBoneSwitchGeneratorDef bsg;
            bsg.name = str(r, "name");
            bsg.userData = intField(r, "userData", 0);
            bsg.pDefaultGenerator = str(r, "pDefaultGenerator");
            bsg.bindings = parseBindings(r);
            if (hasChild(r, "children") && r["children"].is_seq()) {
                std::vector<BoneSwitchChildDef> kids;
                for (auto c : r["children"]) {
                    BoneSwitchChildDef k;
                    k.pGenerator = str(c, "pGenerator");
                    k.boneWeights = parseBoneWeights(c);
                    k.bindings = parseBindings(c);
                    kids.push_back(std::move(k));
                }
                bsg.children = std::move(kids);
            }
            if (auto _k = keyOf(r); !_k.empty()) data.boneSwitchGenerators[_k] = std::move(bsg);
        } else if (cls == "hkbManualSelectorGenerator") {
            ManualSelectorDef s;
            s.name = str(r, "name");
            s.selectedGeneratorIndex = intField(r, "selectedGeneratorIndex", 0);
            s.currentGeneratorIndex  = intField(r, "currentGeneratorIndex", 0);
            s.userData = intField(r, "userData", 0);
            s.bindings = parseBindings(r);
            if (hasChild(r, "generators") && r["generators"].is_seq())
                for (auto g : r["generators"]) {
                    if (!g.has_val()) continue;
                    std::string v; c4::from_chars(g.val(), &v); s.generators.push_back(trim(v));
                }
            if (auto _k = keyOf(r); !_k.empty()) data.selectors[_k] = std::move(s);
        } else if (cls == "BSOffsetAnimationGenerator") {
            BSOffsetAnimationGeneratorDef o;
            o.name                 = str(r, "name");
            o.userData             = intField(r, "userData", 0);
            o.pDefaultGenerator    = str(r, "pDefaultGenerator");
            o.pOffsetClipGenerator = str(r, "pOffsetClipGenerator");
            o.fOffsetVariable      = str(r, "fOffsetVariable", "0.000000");
            o.fOffsetRangeStart    = str(r, "fOffsetRangeStart", "0.000000");
            o.fOffsetRangeEnd      = str(r, "fOffsetRangeEnd", "1.000000");
            o.bindings             = parseBindings(r);
            if (auto _k = keyOf(r); !_k.empty()) data.offsetAnimGenerators[_k] = std::move(o);
        } else if (cls == "BSSynchronizedClipGenerator") {
            BSSynchronizedClipGeneratorDef s;
            s.name                        = str(r, "name");
            s.userData                    = intField(r, "userData", 0);
            s.pClipGenerator              = str(r, "pClipGenerator");
            s.syncAnimPrefix              = str(r, "SyncAnimPrefix");
            s.bSyncClipIgnoreMarkPlacement = boolField(r, "bSyncClipIgnoreMarkPlacement", false);
            s.fGetToMarkTime              = str(r, "fGetToMarkTime", "0.000000");
            s.fMarkErrorThreshold         = str(r, "fMarkErrorThreshold", "0.100000");
            s.bLeadCharacter              = boolField(r, "bLeadCharacter", false);
            s.bReorientSupportChar        = boolField(r, "bReorientSupportChar", false);
            s.bApplyMotionFromRoot        = boolField(r, "bApplyMotionFromRoot", false);
            s.sAnimationBindingIndex      = intField(r, "sAnimationBindingIndex", -1);
            s.bindings                    = parseBindings(r);
            if (auto _k = keyOf(r); !_k.empty()) data.synchronizedClips[_k] = std::move(s);
        } else if (cls == "hkbPoseMatchingGenerator") {
            PoseMatchingGeneratorDef p;
            p.name = str(r, "name");
            p.userData = intField(r, "userData", 0);
            p.referencePoseWeightThreshold = str(r, "referencePoseWeightThreshold", "0.000000");
            p.blendParameter               = str(r, "blendParameter", "0.000000");
            p.minCyclicBlendParameter      = str(r, "minCyclicBlendParameter", "0.000000");
            p.maxCyclicBlendParameter      = str(r, "maxCyclicBlendParameter", "1.000000");
            p.indexOfSyncMasterChild       = intField(r, "indexOfSyncMasterChild", -1);
            p.flags             = str(r, "flags", "0");
            p.subtractLastChild = boolField(r, "subtractLastChild", false);
            if (hasChild(r, "children") && r["children"].is_seq()) {
                std::vector<BlenderChildDef> kids;
                for (auto c : r["children"]) {
                    BlenderChildDef child;
                    child.generator            = str(c, "generator");
                    child.weight               = str(c, "weight", "0.000000");
                    child.worldFromModelWeight = str(c, "worldFromModelWeight", "1.000000");
                    child.boneWeights          = parseBoneWeights(c);
                    child.bindings             = parseBindings(c);
                    kids.push_back(std::move(child));
                }
                p.children = std::move(kids);
            }
            p.worldFromModelRotation = str(r, "worldFromModelRotation", "(0.000000 0.000000 0.000000 1.000000)");
            p.blendSpeed             = str(r, "blendSpeed", "1.000000");
            p.minSpeedToSwitch       = str(r, "minSpeedToSwitch", "0.200000");
            p.minSwitchTimeNoError   = str(r, "minSwitchTimeNoError", "0.200000");
            p.minSwitchTimeFullError = str(r, "minSwitchTimeFullError", "0.000000");
            p.startPlayingEventId  = intField(r, "startPlayingEventId", -1);
            p.startPlayingEvent    = optStr(r, "startPlayingEvent");
            p.startMatchingEventId = intField(r, "startMatchingEventId", -1);
            p.startMatchingEvent   = optStr(r, "startMatchingEvent");
            p.rootBoneIndex    = intField(r, "rootBoneIndex", 0);
            p.otherBoneIndex   = intField(r, "otherBoneIndex", 0);
            p.anotherBoneIndex = intField(r, "anotherBoneIndex", 0);
            p.pelvisIndex      = intField(r, "pelvisIndex", 0);
            p.mode             = str(r, "mode", "MODE_MATCH");
            p.bindings         = parseBindings(r);
            if (auto _k = keyOf(r); !_k.empty()) data.poseMatchingGenerators[_k] = std::move(p);
        } else if (cls == "hkbReferencePoseGenerator") {
            ReferencePoseGeneratorDef p;
            p.name = str(r, "name");
            p.userData = intField(r, "userData", 0);
            p.bindings = parseBindings(r);
            if (auto _k = keyOf(r); !_k.empty()) data.referencePoseGenerators[_k] = std::move(p);
        } else if (cls == "BGSGamebryoSequenceGenerator") {
            BGSGamebryoSequenceGeneratorDef g;
            g.name              = str(r, "name");
            g.userData          = intField(r, "userData", 0);
            g.sequence          = str(r, "sequence");
            g.blendModeFunction = str(r, "blendModeFunction", "BMF_PERCENT");
            g.percent           = str(r, "percent", "1.000000");
            g.bindings          = parseBindings(r);
            if (auto _k = keyOf(r); !_k.empty()) data.gamebryoSequences[_k] = std::move(g);
        } else {
            // default: hkbBlenderGenerator
            BlenderGeneratorDef b;
            b.name = str(r, "name");
            b.flags = static_cast<int>(enums::ResolveEnum(str(r, "flags", "0"), enums::BlenderFlags()));
            b.subtractLastChild = boolField(r, "subtractLastChild", false);
            b.userData = intField(r, "userData", 0);
            b.referencePoseWeightThreshold = str(r, "referencePoseWeightThreshold", "0.000000");
            b.blendParameter               = str(r, "blendParameter", "1.000000");
            b.minCyclicBlendParameter      = str(r, "minCyclicBlendParameter", "0.000000");
            b.maxCyclicBlendParameter      = str(r, "maxCyclicBlendParameter", "1.000000");
            b.indexOfSyncMasterChild       = intField(r, "indexOfSyncMasterChild", -1);
            b.bindings = parseBindings(r);
            if (hasChild(r, "children") && r["children"].is_seq())
                for (auto c : r["children"]) {
                    BlenderChildDef child;
                    child.generator = str(c, "generator");
                    child.weight    = str(c, "weight", "0.000000");
                    child.worldFromModelWeight = str(c, "worldFromModelWeight", "1.000000");
                    child.boneWeights = parseBoneWeights(c);
                    child.bindings = parseBindings(c);
                    b.children.push_back(std::move(child));
                }
            if (auto _k = keyOf(r); !_k.empty()) data.blenders[_k] = std::move(b);
        }
    });

    // ── modifiers/ (class-disambiguated; generic path for the rest) ──
    eachYaml("modifiers", false, [&](const c4::yml::ConstNodeRef& r) {
        std::string cls = peekClass(r);

        if (cls == "hkbModifierGenerator") {
            ModifierGeneratorDef m;
            m.name = str(r, "name");
            m.userData = intField(r, "userData", 0);
            m.modifier  = str(r, "modifier");
            m.generator = str(r, "generator");
            m.bindings  = parseBindings(r);
            if (auto _k = keyOf(r); !_k.empty()) data.modifierGenerators[_k] = std::move(m);
        } else if (cls == "BSIsActiveModifier") {
            BSIsActiveModifierDef m;
            m.name = str(r, "name");
            m.userData = intField(r, "userData", 0);
            m.enable = boolField(r, "enable", true);
            m.bIsActive0 = boolField(r, "bIsActive0"); m.bInvertActive0 = boolField(r, "bInvertActive0");
            m.bIsActive1 = boolField(r, "bIsActive1"); m.bInvertActive1 = boolField(r, "bInvertActive1");
            m.bIsActive2 = boolField(r, "bIsActive2"); m.bInvertActive2 = boolField(r, "bInvertActive2");
            m.bIsActive3 = boolField(r, "bIsActive3"); m.bInvertActive3 = boolField(r, "bInvertActive3");
            m.bIsActive4 = boolField(r, "bIsActive4"); m.bInvertActive4 = boolField(r, "bInvertActive4");
            m.bindings = parseBindings(r);
            if (auto _k = keyOf(r); !_k.empty()) data.isActiveModifiers[_k] = std::move(m);
        } else if (cls == "hkbModifierList") {
            ModifierListDef m;
            m.name = str(r, "name");
            m.userData = intField(r, "userData", 0);
            m.enable = boolField(r, "enable", true);
            if (hasChild(r, "modifiers") && r["modifiers"].is_seq())
                for (auto e : r["modifiers"]) {
                    if (!e.has_val()) continue;
                    std::string v; c4::from_chars(e.val(), &v); m.modifiers.push_back(trim(v));
                }
            m.bindings = parseBindings(r);
            if (auto _k = keyOf(r); !_k.empty()) data.modifierLists[_k] = std::move(m);
        } else if (cls == "hkbEvaluateExpressionModifier") {
            EvaluateExpressionModifierDef m;
            m.name = str(r, "name");
            m.userData = intField(r, "userData", 0);
            m.enable = boolField(r, "enable", true);
            m.expressions = str(r, "expressions");
            m.bindings = parseBindings(r);
            if (auto _k = keyOf(r); !_k.empty()) data.evaluateExpressionModifiers[_k] = std::move(m);
        } else if (cls == "hkbEventDrivenModifier") {
            EventDrivenModifierDef m;
            m.name = str(r, "name");
            m.userData = intField(r, "userData", 0);
            m.enable = boolField(r, "enable", true);
            m.modifier = str(r, "modifier");
            m.activateEventId   = intField(r, "activateEventId", -1);
            m.activateEvent     = optStr(r, "activateEvent");
            m.deactivateEventId = intField(r, "deactivateEventId", -1);
            m.deactivateEvent   = optStr(r, "deactivateEvent");
            m.activeByDefault   = boolField(r, "activeByDefault", false);
            m.bindings = parseBindings(r);
            if (auto _k = keyOf(r); !_k.empty()) data.eventDrivenModifiers[_k] = std::move(m);
        } else if (cls == "hkbFootIkControlsModifier") {
            FootIkControlsModifierDef m;
            m.name = str(r, "name");
            m.userData = intField(r, "userData", 0);
            m.enable = boolField(r, "enable", true);
            m.bindings = parseBindings(r);
            auto ff = [&](const c4::yml::ConstNodeRef& n, const char* k) -> float {
                std::string s = str(n, k, "0"); try { return std::stof(s); } catch (...) { return 0.f; }
            };
            if (hasChild(r, "controlData") && hasChild(r["controlData"], "gains")) {
                auto gn = r["controlData"]["gains"];
                auto& d = m.controlData.gains;
                d.onOffGain = ff(gn, "onOffGain"); d.groundAscendingGain = ff(gn, "groundAscendingGain");
                d.groundDescendingGain = ff(gn, "groundDescendingGain"); d.footPlantedGain = ff(gn, "footPlantedGain");
                d.footRaisedGain = ff(gn, "footRaisedGain"); d.footUnlockGain = ff(gn, "footUnlockGain");
                d.worldFromModelFeedbackGain = ff(gn, "worldFromModelFeedbackGain"); d.errorUpDownBias = ff(gn, "errorUpDownBias");
                d.alignWorldFromModelGain = ff(gn, "alignWorldFromModelGain"); d.hipOrientationGain = ff(gn, "hipOrientationGain");
                d.maxKneeAngleDifference = ff(gn, "maxKneeAngleDifference"); d.ankleOrientationGain = ff(gn, "ankleOrientationGain");
            }
            m.errorOutTranslation     = str(r, "errorOutTranslation", "(0.000000 0.000000 0.000000 0.000000)");
            m.alignWithGroundRotation = str(r, "alignWithGroundRotation", "(0.000000 0.000000 0.000000 0.000000)");
            if (hasChild(r, "legs") && r["legs"].is_seq()) {
                std::vector<FootIkControlsModifierLegDef> legs;
                for (auto lc : r["legs"]) {
                    FootIkControlsModifierLegDef leg;
                    leg.groundPosition = str(lc, "groundPosition", "(0.000000 0.000000 0.000000 0.000000)");
                    if (hasChild(lc, "ungroundedEvent")) leg.ungroundedEvent = parseInlineEvent(lc["ungroundedEvent"]);
                    leg.verticalError = ff(lc, "verticalError");
                    leg.hitSomething  = boolField(lc, "hitSomething", false);
                    leg.isPlantedMS   = boolField(lc, "isPlantedMS", false);
                    legs.push_back(std::move(leg));
                }
                m.legs = std::move(legs);
            }
            if (auto _k = keyOf(r); !_k.empty()) data.footIkControlsModifiers[_k] = std::move(m);
        } else if (cls == "BSEventEveryNEventsModifier") {
            // Wired on the builder side (buildEventEveryN + eventEveryNModifiers +
            // buildNode) but the loader case was missing, so every instance fell to
            // the generic path below and was emitted as a featureless hkbModifier —
            // losing its events and counts. mt_behavior alone has 43 of these.
            BSEventEveryNEventsModifierDef m;
            m.name     = str(r, "name");
            m.userData = intField(r, "userData", 0);
            m.enable   = boolField(r, "enable", true);
            if (hasChild(r, "eventToCheckFor")) m.eventToCheckFor = parseInlineEvent(r["eventToCheckFor"]);
            if (hasChild(r, "eventToSend"))     m.eventToSend     = parseInlineEvent(r["eventToSend"]);
            m.numberOfEventsBeforeSend        = intField(r, "numberOfEventsBeforeSend", 1);
            m.minimumNumberOfEventsBeforeSend = intField(r, "minimumNumberOfEventsBeforeSend", 1);
            m.randomizeNumberOfEvents         = boolField(r, "randomizeNumberOfEvents", false);
            m.bindings = parseBindings(r);
            if (auto _k = keyOf(r); !_k.empty()) data.eventEveryNModifiers[_k] = std::move(m);
        } else if (cls == "BSInterpValueModifier") {
            BSInterpValueModifierDef m;
            m.name     = str(r, "name");
            m.userData = intField(r, "userData", 0);
            m.enable   = boolField(r, "enable", true);
            m.source   = str(r, "source", "0.000000");
            m.target   = str(r, "target", "0.000000");
            m.result   = str(r, "result", "0.000000");
            m.gain     = str(r, "gain", "0.000000");
            m.bindings = parseBindings(r);
            if (auto _k = keyOf(r); !_k.empty()) data.interpValueModifiers[_k] = std::move(m);
        } else if (cls == "hkbEventsFromRangeModifier") {
            EventsFromRangeModifierDef m;
            m.name       = str(r, "name");
            m.userData   = intField(r, "userData", 0);
            m.enable     = boolField(r, "enable", true);
            m.inputValue = str(r, "inputValue", "0.000000");
            m.lowerBound = str(r, "lowerBound", "0.000000");
            m.eventRanges = optStr(r, "eventRanges");
            m.bindings   = parseBindings(r);
            if (auto _k = keyOf(r); !_k.empty()) data.eventsFromRangeModifiers[_k] = std::move(m);
        } else if (cls == "hkbFootIkModifier") {
            FootIkModifierDef m;
            m.name = str(r, "name");
            m.userData = intField(r, "userData", 0);
            m.enable = boolField(r, "enable", true);
            m.bindings = parseBindings(r);
            auto ff = [&](const c4::yml::ConstNodeRef& nn, const char* k) -> float {
                std::string s = str(nn, k, "0"); try { return std::stof(s); } catch (...) { return 0.f; }
            };
            if (hasChild(r, "gains")) {
                auto gn = r["gains"]; auto& d = m.gains;
                d.onOffGain = ff(gn, "onOffGain"); d.groundAscendingGain = ff(gn, "groundAscendingGain");
                d.groundDescendingGain = ff(gn, "groundDescendingGain"); d.footPlantedGain = ff(gn, "footPlantedGain");
                d.footRaisedGain = ff(gn, "footRaisedGain"); d.footUnlockGain = ff(gn, "footUnlockGain");
                d.worldFromModelFeedbackGain = ff(gn, "worldFromModelFeedbackGain"); d.errorUpDownBias = ff(gn, "errorUpDownBias");
                d.alignWorldFromModelGain = ff(gn, "alignWorldFromModelGain"); d.hipOrientationGain = ff(gn, "hipOrientationGain");
                d.maxKneeAngleDifference = ff(gn, "maxKneeAngleDifference"); d.ankleOrientationGain = ff(gn, "ankleOrientationGain");
            }
            m.raycastDistanceUp = ff(r, "raycastDistanceUp");
            m.raycastDistanceDown = ff(r, "raycastDistanceDown");
            m.originalGroundHeightMS = ff(r, "originalGroundHeightMS");
            m.errorOut = ff(r, "errorOut");
            m.errorOutTranslation = str(r, "errorOutTranslation", "(0.000000 0.000000 0.000000 0.000000)");
            m.alignWithGroundRotation = str(r, "alignWithGroundRotation", "(0.000000 0.000000 0.000000 1.000000)");
            m.verticalOffset = ff(r, "verticalOffset");
            m.collisionFilterInfo = static_cast<unsigned>(intField(r, "collisionFilterInfo", 0));
            m.forwardAlignFraction = ff(r, "forwardAlignFraction");
            m.sidewaysAlignFraction = ff(r, "sidewaysAlignFraction");
            m.sidewaysSampleWidth = ff(r, "sidewaysSampleWidth");
            m.useTrackData = boolField(r, "useTrackData", false);
            m.lockFeetWhenPlanted = boolField(r, "lockFeetWhenPlanted", false);
            m.useCharacterUpVector = boolField(r, "useCharacterUpVector", false);
            m.alignMode = intField(r, "alignMode", 0);
            if (hasChild(r, "legs") && r["legs"].is_seq())
                for (auto lc : r["legs"]) {
                    FootIkModifierLegDef leg;
                    leg.prevAnkleRotLS = str(lc, "prevAnkleRotLS", "(0.000000 0.000000 0.000000 1.000000)");
                    leg.kneeAxisLS = str(lc, "kneeAxisLS", "(0.000000 0.000000 0.000000 0.000000)");
                    leg.footEndLS  = str(lc, "footEndLS", "(0.000000 0.000000 0.000000 0.000000)");
                    if (hasChild(lc, "ungroundedEvent")) leg.ungroundedEvent = parseInlineEvent(lc["ungroundedEvent"]);
                    leg.footPlantedAnkleHeightMS = ff(lc, "footPlantedAnkleHeightMS");
                    leg.footRaisedAnkleHeightMS = ff(lc, "footRaisedAnkleHeightMS");
                    leg.maxAnkleHeightMS = ff(lc, "maxAnkleHeightMS");
                    leg.minAnkleHeightMS = ff(lc, "minAnkleHeightMS");
                    leg.maxKneeAngleDegrees = ff(lc, "maxKneeAngleDegrees");
                    leg.minKneeAngleDegrees = ff(lc, "minKneeAngleDegrees");
                    leg.verticalError = ff(lc, "verticalError");
                    leg.maxAnkleAngleDegrees = ff(lc, "maxAnkleAngleDegrees");
                    leg.hipIndex = intField(lc, "hipIndex", 0);
                    leg.kneeIndex = intField(lc, "kneeIndex", 0);
                    leg.ankleIndex = intField(lc, "ankleIndex", 0);
                    leg.hitSomething = boolField(lc, "hitSomething", false);
                    leg.isPlantedMS = boolField(lc, "isPlantedMS", false);
                    leg.isOriginalAnkleTransformMSSet = boolField(lc, "isOriginalAnkleTransformMSSet", false);
                    m.legs.push_back(std::move(leg));
                }
            if (auto _k = keyOf(r); !_k.empty()) data.footIkModifiers[_k] = std::move(m);
        } else if (cls == "BSIStateManagerModifier") {
            BSIStateManagerModifierDef m;
            m.name      = str(r, "name");
            m.userData  = intField(r, "userData", 0);
            m.enable    = boolField(r, "enable", true);
            m.iStateVar      = intField(r, "iStateVar", 0);
            m.iStateVariable = optStr(r, "iStateVariable");
            if (hasChild(r, "stateData") && r["stateData"].is_seq())
                for (auto e : r["stateData"]) {
                    IStateDataDef sd;
                    sd.pStateMachine = str(e, "pStateMachine");
                    sd.StateID       = intField(e, "StateID", 0);
                    sd.iStateToSetAs = intField(e, "iStateToSetAs", 0);
                    m.stateData.push_back(std::move(sd));
                }
            m.bindings = parseBindings(r);
            if (auto _k = keyOf(r); !_k.empty()) data.iStateManagerModifiers[_k] = std::move(m);
        } else {
            // generic modifier (e.g. hkbTwistModifier) — base fields + extra params.
            GenericModifierDef m;
            m.className = cls;
            m.name = str(r, "name");
            m.userData = intField(r, "userData", 0);
            m.enable = boolField(r, "enable", true);
            m.bindings = parseBindings(r);
            m.extraParams = parseGenericExtraParams(r);
            if (auto _k = keyOf(r); !_k.empty()) data.genericModifiers[_k] = std::move(m);
        }
    });

    // ── references/ ──
    eachYaml("references", true, [&](const c4::yml::ConstNodeRef& r) {
        BehaviorReferenceGeneratorDef b;
        b.name = str(r, "name");
        b.userData = intField(r, "userData", 0);
        b.behaviorName = str(r, "behaviorName");
        b.bindings = parseBindings(r);
        if (auto _k = keyOf(r); !_k.empty()) data.behaviorReferences[_k] = std::move(b);
    });

    // ── tagging/ ──
    eachYaml("tagging", true, [&](const c4::yml::ConstNodeRef& r) {
        BSiStateTaggingGeneratorDef g;
        g.name = str(r, "name");
        g.userData = intField(r, "userData", 0);
        g.pDefaultGenerator = str(r, "pDefaultGenerator");
        g.iStateToSetAs = intField(r, "iStateToSetAs", 0);
        g.iPriority     = intField(r, "iPriority", 0);
        g.bindings = parseBindings(r);
        if (auto _k = keyOf(r); !_k.empty()) data.stateTaggingGenerators[_k] = std::move(g);
    });

    // ── states/ (StateMachine + StateInfo, disambiguated by class) ──
    eachYaml("states", false, [&](const c4::yml::ConstNodeRef& r) {
        std::string cls = peekClass(r);
        if (cls == "hkbStateMachine") {
            StateMachineDef sm;
            sm.name = str(r, "name");
            sm.userData = intField(r, "userData", 0);
            sm.startStateId = intField(r, "startStateId", 0);
            sm.eventToSendWhenStateOrTransitionChangesId = intField(r, "eventToSendWhenStateOrTransitionChanges", -1);
            sm.eventToSendWhenStateOrTransitionChangesEvent = optStr(r, "eventToSendWhenStateOrTransitionChangesEvent");
            sm.returnToPreviousStateEventId       = intField(r, "returnToPreviousStateEventId", -1);
            sm.returnToPreviousStateEvent         = optStr(r, "returnToPreviousStateEvent");
            sm.randomTransitionEventId            = intField(r, "randomTransitionEventId", -1);
            sm.randomTransitionEvent              = optStr(r, "randomTransitionEvent");
            sm.transitionToNextHigherStateEventId = intField(r, "transitionToNextHigherStateEventId", -1);
            sm.transitionToNextHigherStateEvent   = optStr(r, "transitionToNextHigherStateEvent");
            sm.transitionToNextLowerStateEventId  = intField(r, "transitionToNextLowerStateEventId", -1);
            sm.transitionToNextLowerStateEvent    = optStr(r, "transitionToNextLowerStateEvent");
            sm.syncVariableIndex = intField(r, "syncVariableIndex", -1);
            sm.syncVariable      = optStr(r, "syncVariable");
            sm.wrapAroundStateId = boolField(r, "wrapAroundStateId", false);
            sm.maxSimultaneousTransitions = intField(r, "maxSimultaneousTransitions", 32);
            sm.startStateMode     = str(r, "startStateMode", "START_STATE_MODE_DEFAULT");
            sm.selfTransitionMode = str(r, "selfTransitionMode", "SELF_TRANSITION_MODE_NO_TRANSITION");
            sm.bindings = parseBindings(r);
            if (hasChild(r, "states") && r["states"].is_seq())
                for (auto s : r["states"]) {
                    if (!s.has_val()) continue;
                    std::string v; c4::from_chars(s.val(), &v); sm.states.push_back(trim(v));
                }
            if (hasChild(r, "transitions"))
                sm.parsedWildcardTransitions = parseTransitionsSeq(r["transitions"]);
            if (auto _k = keyOf(r); !_k.empty()) data.stateMachines[_k] = std::move(sm);
        } else {
            StateDef st;
            st.name = str(r, "name");
            st.stateId = intField(r, "stateId", 0);
            st.generator = str(r, "generator");
            st.probability = str(r, "probability", "1.000000");
            st.enable = boolField(r, "enable", true);
            st.enterNotifyEvents = parseEventProps(r, "enterNotifyEvents");
            st.exitNotifyEvents  = parseEventProps(r, "exitNotifyEvents");
            if (hasChild(r, "transitions"))
                st.parsedTransitions = parseTransitionsSeq(r["transitions"]);
            if (hasChild(r, "entryTransitions"))
                st.entryTransitions = parseTransitionsSeq(r["entryTransitions"]);
            st.bindings = parseBindings(r);
            if (hasChild(r, "parents") && r["parents"].is_seq())
                for (auto p : r["parents"]) {
                    if (!p.has_val()) continue;
                    std::string v; c4::from_chars(p.val(), &v); st.parents.push_back(trim(v));
                }
            if (auto _k = keyOf(r); !_k.empty()) data.states[_k] = std::move(st);
        }
    });

    // ── data/ auxiliary arrays (hkbExpressionDataArray + hkbBoneIndexArray;
    //    graphdata.yaml has no `class:` so peekClass skips it). Bone-index arrays
    //    store bone NAMES here; the builder resolves them against data.boneNames. ──
    eachYaml("data", false, [&](const c4::yml::ConstNodeRef& r) {
        const std::string cls = peekClass(r);
        if (cls == "hkbExpressionDataArray") {
            ExpressionDataArrayDef e;
            e.name = str(r, "name");
            if (hasChild(r, "expressionsData") && r["expressionsData"].is_seq())
                for (auto d : r["expressionsData"]) {
                    ExpressionDataDef ed;
                    ed.expression              = str(d, "expression");
                    ed.assignmentVariableIndex = intField(d, "assignmentVariableIndex", -1);
                    ed.assignmentVariable      = optStr(d, "assignmentVariable");
                    ed.assignmentEventIndex    = intField(d, "assignmentEventIndex", -1);
                    ed.assignmentEvent         = optStr(d, "assignmentEvent");
                    ed.eventMode               = str(d, "eventMode", "EVENT_MODE_SEND_ONCE");
                    e.expressionsData.push_back(std::move(ed));
                }
            if (auto _k = keyOf(r); !_k.empty()) data.expressionDataArrays[_k] = std::move(e);
        } else if (cls == "hkbEventRangeDataArray") {
            EventRangeDataArrayDef e;
            e.name = str(r, "name");
            if (hasChild(r, "eventData") && r["eventData"].is_seq())
                for (auto d : r["eventData"]) {
                    EventRangeDef ed;
                    ed.upperBound = str(d, "upperBound", "0.000000");
                    ed.eventId    = intField(d, "eventId", -1);
                    ed.event      = optStr(d, "event");
                    ed.payload    = optStr(d, "payload");
                    ed.eventMode  = str(d, "eventMode", "EVENT_MODE_SEND_ONCE");
                    e.eventData.push_back(std::move(ed));
                }
            if (auto _k = keyOf(r); !_k.empty()) data.eventRangeDataArrays[_k] = std::move(e);
        } else if (cls == "hkbBoneIndexArray") {
            BoneIndexArrayDef b;
            b.name = str(r, "name");
            // Entries are bone NAMES (vanilla source) or raw INDICES (our decompile).
            if (hasChild(r, "boneIndices") && r["boneIndices"].is_seq())
                for (auto n : r["boneIndices"]) {
                    if (!n.has_val()) continue;
                    std::string v; c4::from_chars(n.val(), &v); v = trim(v);
                    int idx;
                    if (!v.empty() && (std::isdigit((unsigned char)v[0]) || v[0] == '-') &&
                        std::sscanf(v.c_str(), "%d", &idx) == 1 && std::to_string(idx) == v)
                        b.boneIndices.push_back(idx);
                    else
                        b.boneNames.push_back(v);
                }
            if (auto _k = keyOf(r); !_k.empty()) data.boneIndexArrays[_k] = std::move(b);
        }
    });

    // ── data/graphdata.yaml (per layer; last-writer for step 0b — step 3 unions) ──
    if (data.behavior.behavior.data && *data.behavior.behavior.data != "null")
        for (const auto& src : sources) {
        std::optional<std::string> dtext = src->read("data/" + *data.behavior.behavior.data + ".yaml");
        if (dtext) {
            std::string text = std::move(*dtext);
            c4::yml::Tree tree = c4::yml::parse_in_place(c4::to_substr(text));
            auto r = tree.rootref();
            BehaviorGraphDataDef gd;
            if (hasChild(r, "variables") && r["variables"].is_seq())
                for (auto v : r["variables"]) {
                    VariableInfoDef vi;
                    vi.name = str(v, "name");
                    vi.type = str(v, "type", "VARIABLE_TYPE_REAL");
                    vi.role = str(v, "role", "ROLE_DEFAULT");
                    vi.roleFlags = intField(v, "roleFlags", 0);
                    vi.value = intField(v, "value", 0);
                    vi.quadValue = optStr(v, "quadValue");
                    gd.variables.push_back(std::move(vi));
                }
            if (hasChild(r, "events") && r["events"].is_seq())
                for (auto e : r["events"]) {
                    EventInfoDef ei;
                    ei.name = str(e, "name");
                    ei.flags = str(e, "flags", "0");
                    gd.events.push_back(std::move(ei));
                }
            if (hasChild(r, "characterPropertyNames") && r["characterPropertyNames"].is_seq())
                for (auto cp : r["characterPropertyNames"]) {
                    CharacterPropertyDef c;
                    c.name = str(cp, "name");
                    c.type = str(cp, "type", "VARIABLE_TYPE_POINTER");
                    c.flags = str(cp, "flags", "0");
                    gd.characterPropertyNames.push_back(std::move(c));
                }
            gd.characterPropertyInfoCount = intField(r, "characterPropertyInfos", 0);
            gd.attributeDefaultCount      = intField(r, "attributeDefaults", 0);
            gd.variantVariableValueCount  = intField(r, "variantVariableValues", 0);
            gd.wordMinVariableValueCount  = intField(r, "wordMinVariableValues", 0);
            gd.wordMaxVariableValueCount  = intField(r, "wordMaxVariableValues", 0);
            if (hasChild(r, "quadVariableValues") && r["quadVariableValues"].is_seq())
                for (auto q : r["quadVariableValues"]) {
                    if (!q.has_val()) continue;
                    std::string v; c4::from_chars(q.val(), &v);
                    gd.quadVariableValues.push_back(trim(v));
                }
            data.graphData = std::move(gd);
        }
    }

    // ── data/additive.yaml — native graph-vocab UNION (step 3) ──────────────────
    // A mod delta adds events / variables / characterProperties by shipping ONLY the
    // additions in data/additive.yaml (never a full graphdata.yaml, which would clobber
    // the base tables and shift every $eventID). Union = append + dedup by name, so every
    // pre-existing index is preserved and $eventID / $variableID stay valid. Runs per
    // layer in load order over the base tables set above. (animationNames is character-
    // scoped — hkbCharacterStringData — and unions separately via the roster injector.)
    {
        auto& gdOpt = data.graphData;
        std::unordered_set<std::string> haveEv, haveVar, haveCp;
        if (gdOpt) {
            for (const auto& e : gdOpt->events)                 haveEv.insert(e.name);
            for (const auto& v : gdOpt->variables)              haveVar.insert(v.name);
            for (const auto& c : gdOpt->characterPropertyNames) haveCp.insert(c.name);
        }
        for (const auto& src : sources) {
            std::optional<std::string> atext = src->read("data/additive.yaml");
            if (!atext) continue;
            std::string text = std::move(*atext);
            c4::yml::Tree tree = c4::yml::parse_in_place(c4::to_substr(text));
            auto r = tree.rootref();
            if (!gdOpt) gdOpt.emplace();                 // additive with no base graphdata
            if (hasChild(r, "events") && r["events"].is_seq())
                for (auto e : r["events"]) {
                    std::string nm = str(e, "name");
                    if (nm.empty() || haveEv.count(nm)) continue;
                    haveEv.insert(nm);
                    EventInfoDef ei; ei.name = nm; ei.flags = str(e, "flags", "0");
                    gdOpt->events.push_back(std::move(ei));
                }
            if (hasChild(r, "variables") && r["variables"].is_seq())
                for (auto v : r["variables"]) {
                    std::string nm = str(v, "name");
                    if (nm.empty() || haveVar.count(nm)) continue;
                    haveVar.insert(nm);
                    VariableInfoDef vi;
                    vi.name      = nm;
                    vi.type      = str(v, "type", "VARIABLE_TYPE_REAL");
                    vi.role      = str(v, "role", "ROLE_DEFAULT");
                    vi.roleFlags = intField(v, "roleFlags", 0);
                    vi.value     = intField(v, "value", 0);
                    vi.quadValue = optStr(v, "quadValue");
                    gdOpt->variables.push_back(std::move(vi));
                }
            // characterProperties — guard the count-only base (M4): appending a NAME to a
            // base that has only a count (no names) would flip charPropCount to names.size()
            // in the builder and drop the unnamed base slots. Refuse loudly, don't corrupt.
            if (hasChild(r, "characterPropertyNames") && r["characterPropertyNames"].is_seq())
                for (auto cp : r["characterPropertyNames"]) {
                    std::string nm = str(cp, "name");
                    if (nm.empty() || haveCp.count(nm)) continue;
                    if (gdOpt->characterPropertyNames.empty() && gdOpt->characterPropertyInfoCount > 0)
                        throw std::runtime_error(
                            "YamlBehaviorLoader: additive characterProperty '" + nm + "' on a count-only "
                            "base (characterPropertyInfos=" + std::to_string(gdOpt->characterPropertyInfoCount) +
                            ", no names) — refusing (would drop the unnamed base slots). Name the base first.");
                    haveCp.insert(nm);
                    CharacterPropertyDef c;
                    c.name  = nm;
                    c.type  = str(cp, "type", "VARIABLE_TYPE_POINTER");
                    c.flags = str(cp, "flags", "0");
                    gdOpt->characterPropertyNames.push_back(std::move(c));
                }
        }
    }

    // ── bone_presets.yaml (per layer; last-writer per preset) ──
    for (const auto& src : sources) {
        std::optional<std::string> ptext = src->read("bone_presets.yaml");
        if (ptext) {
            std::string text = std::move(*ptext);
            c4::yml::Tree tree = c4::yml::parse_in_place(c4::to_substr(text));
            auto r = tree.rootref();
            if (hasChild(r, "presets")) {
                auto presets = r["presets"];
                if (presets.is_map())
                    for (auto p : presets) {
                        std::string presetName; c4::from_chars(p.key(), &presetName);
                        std::map<std::string, std::string> m;
                        if (p.is_map())
                            for (auto kv : p) {
                                std::string k, v;
                                c4::from_chars(kv.key(), &k);
                                if (kv.has_val()) c4::from_chars(kv.val(), &v);
                                m[k] = v;
                            }
                        data.bonePresets[presetName] = std::move(m);
                    }
            }
        }
    }

    // ── skeleton bone names (per layer; last non-empty wins) ──
    for (const auto& src : sources)
        if (auto bn = findAndLoadSkeleton(*src); !bn.empty()) data.boneNames = std::move(bn);

    // ── post-load resolution (mirror BehaviorReader) ──
    // (1) expand boneWeights.preset -> named on blender & bone-switch children.
    //
    // OPTIONAL / CURRENTLY-UNUSED: the preset feature is dormant — no bone_presets.yaml
    // ships anywhere and every real bone-weight source is raw, so this lambda is a no-op
    // in practice. It is kept as a documented nice-to-have. INVARIANT: a preset that
    // names a non-existent entry is a HARD ERROR — silently leaving it unexpanded would
    // drop the mask (IsPreset() stays true but named is never set), so if a mod ever
    // authors a bone_presets.yaml, a typo fails loudly here instead of downstream.
    auto expandPreset = [&](std::optional<BoneWeightsDef>& bw) {
        if (!bw || !bw->IsPreset()) return;
        auto it = data.bonePresets.find(*bw->preset);
        if (it == data.bonePresets.end())
            throw std::runtime_error(
                "YamlBehaviorLoader: boneWeights preset '" + *bw->preset +
                "' is not defined in any bone_presets.yaml — cannot expand.");
        bw->named = it->second;
    };
    for (auto& [name, blend] : data.blenders)
        for (auto& child : blend.children) expandPreset(child.boneWeights);
    for (auto& [name, bsg] : data.boneSwitchGenerators)
        if (bsg.children) for (auto& child : *bsg.children) expandPreset(child.boneWeights);

    // (1b) ownership inversion — collect states that name a parent SM into that SM's
    // `states` list (dedup), so a mod adds a state as its own namespaced file without
    // editing the shared SM node. Decompiled states carry no `parents`, so this is a
    // no-op for existing bundles; data.states is a std::map (sorted keys), so the append
    // order is deterministic.
    for (auto& [stateKey, st] : data.states)
        for (const auto& smRef : st.parents) {
            // Resolve the insertion target by map KEY first, then by NAME. A decompiled base
            // SM (Skyrim.hky) is keyed by its numeric id but carries name: 'Master_Behavior',
            // so a mod naming its parent "Master_Behavior" must match the name field — that
            // single name->target lookup is the ONLY cross-namespace resolution the insertion
            // model needs; the compiler mints the inserted state's final id, and inter-hkx
            // refs go by path (behaviorName), so no general name<->id alias is required.
            StateMachineDef* sm = nullptr;
            if (auto it = data.stateMachines.find(smRef); it != data.stateMachines.end())
                sm = &it->second;
            else
                for (auto& [k, cand] : data.stateMachines)
                    if (cand.name == smRef) { sm = &cand; break; }
            if (!sm) {   // never silently drop — a missing parent means the edit vanishes
                emitMergeDiag("ownership inversion: state '" + stateKey + "' names parent SM '" + smRef +
                              "' but no state machine with that key or name exists — state NOT inserted");
                continue;
            }
            auto& list = sm->states;
            if (std::find(list.begin(), list.end(), stateKey) == list.end())
                list.push_back(stateKey);

            // Transition inversion (the mirror of the state insertion above): hoist this
            // state's entry wildcards into the parent SM's wildcard transitions. This is how a
            // child adds the TRIGGER that reaches its state without editing the shared id-keyed
            // vanilla SM — an explicit append (never an array-replace), so vanilla's own
            // wildcards are untouched. toState defaults to this state (the wildcards enter it);
            // step (2) below resolves the toStateId once banding has minted the state's id.
            if (st.entryTransitions) {
                if (!sm->parsedWildcardTransitions) sm->parsedWildcardTransitions.emplace();
                for (TransitionInfoDef t : *st.entryTransitions) {
                    if (!t.toState || t.toState->empty()) t.toState = stateKey;
                    sm->parsedWildcardTransitions->push_back(std::move(t));
                }
            }
        }

    // (1c) stateId banding — within each SM keep the first occurrence of a numeric
    // stateId (explicit/base states are listed first, so they keep theirs) and reassign
    // any later collision to max+1. Two mods independently adding a state to one SM would
    // otherwise pick colliding ids; transitions target the state by key and pick up the
    // banded id in (2) below, so the reassignment is transparent (BR mints the slot).
    for (auto& [smKey, sm] : data.stateMachines) {
        std::vector<int> used;
        int maxId = -1;
        for (const auto& stateKey : sm.states) {
            auto it = data.states.find(stateKey);
            if (it == data.states.end()) continue;
            int sid = it->second.stateId;
            if (std::find(used.begin(), used.end(), sid) != used.end()) {
                sid = maxId + 1;
                it->second.stateId = sid;
            }
            used.push_back(sid);
            if (sid > maxId) maxId = sid;
        }
    }

    // (2) resolve transition toState -> toStateId using each SM's state list.
    auto resolveToStates = [&](StateMachineDef& sm) {
        std::map<std::string, int> nameToId;
        for (const auto& sname : sm.states) {
            auto it = data.states.find(sname);
            if (it != data.states.end()) nameToId[sname] = it->second.stateId;
        }
        auto fix = [&](std::vector<TransitionInfoDef>& ts) {
            for (auto& t : ts)
                if (t.toState) {
                    auto it = nameToId.find(*t.toState);
                    if (it != nameToId.end()) t.toStateId = it->second;
                }
        };
        for (const auto& sname : sm.states) {
            auto it = data.states.find(sname);
            if (it != data.states.end() && it->second.parsedTransitions)
                fix(*it->second.parsedTransitions);
        }
        if (sm.parsedWildcardTransitions) fix(*sm.parsedWildcardTransitions);
    };
    for (auto& [name, sm] : data.stateMachines) resolveToStates(sm);

}

BehaviorData YamlBehaviorLoader::Load(const std::string& directory) {
    return LoadMerged(std::vector<std::string>{ directory });
}

// Merge: load `dirs` in priority order (base first) into one BehaviorData. Each node
// is gathered across all layers by keyOf and merged in one pass (see loadDirInto's
// eachYaml seam); a later layer's same-key node wins/bash-merges, a new node is added.
// Only the first (base) dir must contain behavior.yaml. Disk overload: validate each
// dir exists (byte-identical to the old inline check), then wrap in DiskUnitSource and
// forward to the source-based overload.
BehaviorData YamlBehaviorLoader::LoadMerged(const std::vector<std::string>& dirs) {
    for (const auto& d : dirs)
        if (!fs::is_directory(d))
            throw std::runtime_error("YamlBehaviorLoader: behavior directory not found: " + d);
    std::vector<std::shared_ptr<const IUnitSource>> sources;
    sources.reserve(dirs.size());
    for (const auto& d : dirs)
        sources.push_back(std::make_shared<DiskUnitSource>(fs::path(d)));
    return LoadMerged(sources);
}

BehaviorData YamlBehaviorLoader::LoadMerged(const std::vector<std::shared_ptr<const IUnitSource>>& sources) {
    BehaviorData data;
    loadDirInto(data, sources, /*requireRoot*/ true);
    return data;
}

std::vector<YamlBehaviorLoader::NodeContribution>
YamlBehaviorLoader::NodeContributions(const std::vector<std::shared_ptr<const IUnitSource>>& sources) {
    // Sections + recursion flags — the section LIST must still mirror loadDirInto's eachYaml(...)
    // calls above (keep in sync when a section is added). The SCAN itself no longer can drift: both
    // paths go through the shared scanSourceSection (same keyOf/peekClass/'\x1f' identity). Stage 2 of
    // the discovery refactor removes even this list-mirror by dispatching on the node's own class.
    static constexpr struct { const char* sub; bool recursive; } kSections[] = {
        { "clips", true }, { "selectors", true }, { "transitions", true }, { "generators", true },
        { "modifiers", false }, { "references", true }, { "tagging", true }, { "states", false },
        { "data", false },
    };

    struct Acc { std::string section, cls, key; std::vector<std::size_t> layers; };
    std::vector<Acc>                             accs;
    std::unordered_map<std::string, std::size_t> index;   // section\x1f class\x1f key -> accs idx

    for (std::size_t li = 0; li < sources.size(); ++li) {
        const auto& src = sources[li];
        if (!src) continue;
        for (const auto& sec : kSections) {
            // Same shared scan the loader uses (scanSourceSection) — no more parallel parse/keyOf/
            // peekClass to keep in sync. Layer/section loop order preserved (li outer, sec inner).
            scanSourceSection(*src, sec.sub, sec.recursive,
                [&](std::string cls, std::string k, std::string /*text*/) {
                    std::string mapKey = std::string(sec.sub) + '\x1f' + cls + '\x1f' + k;
                    auto it = index.find(mapKey);
                    std::size_t ai;
                    if (it == index.end()) {
                        ai = accs.size();
                        index.emplace(std::move(mapKey), ai);
                        accs.push_back({ sec.sub, std::move(cls), std::move(k), {} });
                    } else {
                        ai = it->second;
                    }
                    auto& L = accs[ai].layers;
                    if (L.empty() || L.back() != li) L.push_back(li);  // ascending; dedup same-layer dupes
                });
        }
    }

    std::vector<NodeContribution> out;
    for (auto& a : accs)
        if (a.layers.size() >= 2)                                 // only cross-layer overlaps are conflicts
            out.push_back({ std::move(a.section), std::move(a.cls), std::move(a.key), std::move(a.layers) });
    return out;
}

void YamlBehaviorLoader::SetDiagnosticSink(std::function<void(const std::string&)> sink) {
    g_mergeDiag = std::move(sink);
}

void YamlBehaviorLoader::SetSchemaRegistry(const schema::SchemaRegistry* reg, bool strict) {
    g_mergeSchema = reg;
    g_mergeStrict = strict;
}

} // namespace havok::model

// ── sct::ReadClipInputsFromClipsDir ───────────────────────────────────────────
// Implemented in THIS TU (not AnimDataFromBehavior.cpp) so the clip-node YAML parse
// reuses the file-local ryml helpers (str/parseTriggers/parseNamed) and ryml stays
// isolated to YamlBehaviorLoader. Reads each clips/*.yaml as an independent
// hkbClipGenerator node — no graph load, no base-merge — which is exactly what a mod
// behavior DELTA unit (no behavior.yaml) needs. Maps every field the same way the
// clips/ section of Load() does, then onto DeriveClipInput like
// DeriveClipInputsFromBehavior. Never throws (a bad file is skipped).
namespace havok::sct {

std::vector<havok::animdata::DeriveClipInput> ReadClipInputsFromClipsDir(const std::string& clipsDir) {
    namespace fs = std::filesystem;

    std::vector<havok::animdata::DeriveClipInput> out;
    std::error_code ec;
    if (!fs::is_directory(clipsDir, ec)) return out;

    for (fs::directory_iterator it(clipsDir, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        const fs::path& p = it->path();
        if (p.extension() != ".yaml" && p.extension() != ".yml") continue;

        std::ifstream f(p, std::ios::binary);
        if (!f) continue;
        std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
        if (text.empty()) continue;

        try {
            c4::yml::Tree tree = havok::model::parseNamed(text, p);
            auto r = tree.rootref();
            if (!r.readable() || !r.is_map()) continue;
            // Only genuine clip generators — a clips/ dir should hold nothing else, but
            // skip anything mislabeled so a stray file can't fabricate a clip.
            if (havok::model::peekClass(r) != "hkbClipGenerator") continue;

            havok::animdata::DeriveClipInput dc;
            dc.name          = havok::model::str(r, "name");
            dc.animationName = havok::model::str(r, "animationName");
            dc.playbackSpeed = std::atof(havok::model::str(r, "playbackSpeed", "1.000000").c_str());
            dc.cropStart     = std::atof(havok::model::str(r, "cropStartAmountLocalTime", "0.000000").c_str());
            dc.cropEnd       = std::atof(havok::model::str(r, "cropEndAmountLocalTime", "0.000000").c_str());

            // Clip generator's OWN authored triggers (annotation triggers are the caller's
            // job — DeriveProjectClipList merges them from the animation file). An event with
            // no resolved name contributes nothing to the cache text, so skip it.
            if (auto trg = havok::model::parseTriggers(r)) {
                for (const auto& t : *trg) {
                    if (!t.event || t.event->empty()) continue;
                    havok::animdata::DeriveClipInput::Trigger tr;
                    tr.event               = *t.event;
                    tr.localTime           = std::atof(t.localTime.c_str());
                    tr.relativeToEndOfClip = t.relativeToEndOfClip;
                    tr.fromAnnotation      = false;
                    dc.triggers.push_back(std::move(tr));
                }
            }

            if (!dc.name.empty()) out.push_back(std::move(dc));
        } catch (...) {
            // malformed file — skip, mirroring the "never throws" contract
        }
    }
    return out;
}

} // namespace havok::sct
