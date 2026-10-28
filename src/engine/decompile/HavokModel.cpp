#include <decompile/BehaviorDecompile.h>

#include <interface/HavokEnums.h>       // enum tables (value<->name), revNum, FormatFlags, ResolveEnum — reused
#include <common/Xml.h>                // first-party tagfile XML parser (xml::Node / xml::Parse)
#include <decompile/PandoraCompatShim.h> // CONVERTER-ONLY Nemesis text-array placement (Pandora parity)
#include <interface/BashMerge.h>        // the Nemesis tagfile merge core (bashMerge / decideParam / changedFields)
#include <interface/linker/Linker.h>      // the ONE name<->index codec (roster decode + dup-guard)
#include <interface/BoneOrder.h>
#include <codec/formid/FormId.h>          // CB::engine::codec::formid — node identity as a FormId "idx:local"
#include <codec/resource/PathKey.h>       // HasExt — Nemesis `#*.txt` patch files (path identity)
#include <common/Vec4Text.h>              // CB::engine::common::vec4::parseVec4 (the ONE vec4 text parser)
#include <RymlInclude.h>                  // ParseHky — the reflective .hky reader
#include <codec/serialization/packfile/PackFileSerializer.h>   // RoundTripReflective re-serialize
#include <disk/Disk.h>
#include <text/Text.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace CB::engine::decompile {
using namespace CB::engine::codec;
using namespace CB::engine::common;

bool CharPropNeutral(const schema::Field& f, const io::FieldValue& v) {
    if (f.charProp.empty()) return false;
    if (f.kind == schema::FieldKind::Scalar) {
        std::int64_t x = 0;
        const int w = schema::ScalarWidth(f.scalar);
        if (static_cast<int>(v.raw.size()) != w) return false;
        for (int i = 0; i < w; ++i) x |= static_cast<std::int64_t>(v.raw[static_cast<std::size_t>(i)]) << (8 * i);
        if (w < 8 && (x >> (8 * w - 1)) & 1) x |= ~((std::int64_t(1) << (8 * w)) - 1);
        return x == -1;
    }
    if (!v.obj) return true;
    const auto* so = dynamic_cast<const io::SchemaObject*>(v.obj.get());
    if (!so || so->ClassName() != f.charProp) return false;
    const auto fields = so->Fields();
    const auto& vals  = so->Values();
    for (std::size_t i = 0; i < fields.size() && i < vals.size(); ++i) {
        const auto k = fields[i]->kind;
        if (k == schema::FieldKind::Ptr && (vals[i].obj || !vals[i].objs.empty())) return false;
        if ((k == schema::FieldKind::ScalarArray || k == schema::FieldKind::PtrArray || k == schema::FieldKind::StructArray) &&
            (!vals[i].raw.empty() || !vals[i].objs.empty()))
            return false;
    }
    return true;
}

std::string CategoryForClass(const std::string& className) {
    // The .hky folder each top-level node class lives in, extracted verbatim from the reference
    // BehaviorDecompiler's node()/effect()/stateInfo() emit sites. A class NOT in this map is inlined
    // into its owner's YAML (transition arrays, event-property arrays, conditions, blender children,
    // bone weight/index arrays, the graph/root/string-data) — it still consumes a tagfile id but gets
    // no file, so its category is "".
    static const std::unordered_map<std::string, std::string> kMap = {
        // states
        {"hkbStateMachine", "states"}, {"hkbStateMachineStateInfo", "states"},
        // clips
        {"hkbClipGenerator", "clips"},
        // transition effects
        {"hkbTransitionEffect", "transitions"}, {"hkbBlendingTransitionEffect", "transitions"},
        // selectors / references / tagging
        {"hkbManualSelectorGenerator", "selectors"}, {"CBPoseSearchGenerator", "selectors"},
        {"hkbBehaviorReferenceGenerator", "references"},
        {"BSiStateTaggingGenerator", "tagging"},
        // generators
        {"hkbBlenderGenerator", "generators"}, {"BSCyclicBlendTransitionGenerator", "generators"},
        {"BSBoneSwitchGenerator", "generators"}, {"BSOffsetAnimationGenerator", "generators"},
        {"BSSynchronizedClipGenerator", "generators"}, {"hkbPoseMatchingGenerator", "generators"},
        {"hkbReferencePoseGenerator", "generators"}, {"BGSGamebryoSequenceGenerator", "generators"},
        {"hkbModifierGenerator", "modifiers"},   // decompiler writes it to modifiers/ (generator-style body)
        // modifiers
        {"hkbModifierList", "modifiers"}, {"BSIsActiveModifier", "modifiers"},
        {"hkbEvaluateExpressionModifier", "modifiers"}, {"hkbEventsFromRangeModifier", "modifiers"},
        {"hkbEventDrivenModifier", "modifiers"}, {"hkbFootIkControlsModifier", "modifiers"},
        {"hkbFootIkModifier", "modifiers"}, {"BSIStateManagerModifier", "modifiers"},
        {"hkbTwistModifier", "modifiers"}, {"hkbDampingModifier", "modifiers"},
        {"hkbRotateCharacterModifier", "modifiers"}, {"hkbGetUpModifier", "modifiers"},
        {"BSDirectAtModifier", "modifiers"}, {"BSEventOnDeactivateModifier", "modifiers"},
        {"BSEventOnFalseToTrueModifier", "modifiers"}, {"BSSpeedSamplerModifier", "modifiers"},
        {"BSModifyOnceModifier", "modifiers"}, {"hkbTimerModifier", "modifiers"},
        {"BSRagdollContactListenerModifier", "modifiers"},
        {"hkbRigidBodyRagdollControlsModifier", "modifiers"},
        {"hkbPoweredRagdollControlsModifier", "modifiers"}, {"hkbKeyframeBonesModifier", "modifiers"},
        {"BSLookAtModifier", "modifiers"}, {"BSInterpValueModifier", "modifiers"},
        {"BSEventEveryNEventsModifier", "modifiers"}, {"BSGetTimeStepModifier", "modifiers"},
        {"hkbTransformVectorModifier", "modifiers"}, {"BSDecomposeVectorModifier", "modifiers"},
        {"BSLimbIKModifier", "modifiers"}, {"BSTweenerModifier", "modifiers"},
        {"BSPassByTargetTriggerModifier", "modifiers"}, {"BSTimerModifier", "modifiers"},
        {"CBDamageModifier", "modifiers"},
    };
    auto it = kMap.find(className);
    return it == kMap.end() ? std::string{} : it->second;
}

Identity AssignIdentity(PackFileDeserializer& des, const schema::SchemaRegistry& /*reg*/) {
    Identity out;
    const auto& byOff = des.DeserializedObjects();   // offset -> object
    /// @note Post-order (read-completion) numbering from ClassnamesCount: stable for the same bytes, which is all a
    ///       unit's identity needs — FormIDs are assigned once, at emit, and patches derive against them.
    const std::uint32_t base = static_cast<std::uint32_t>(des.ClassnamesCount());
    std::uint32_t rootOff = std::numeric_limits<std::uint32_t>::max();
    for (const auto& [off, cls] : des.ListObjects())
        if (cls == "hkRootLevelContainer") { rootOff = off; break; }
    const auto& completion = des.ReadCompletionOrder();
    std::uint32_t rank = base + 1;
    for (const std::uint32_t off : completion) {
        auto it = byOff.find(off);
        if (it == byOff.end()) continue;
        IHavokObject* obj = it->second.get();
        const int id = (off == rootOff) ? static_cast<int>(base) : static_cast<int>(rank++);
        out.ids[obj]      = std::to_string(id);
        out.category[obj] = CategoryForClass(obj->ClassName());
    }
    return out;
}

// ── .hky value rendering (ported from BehaviorDecompiler's helpers) ───────────
namespace {
namespace en = CB::engine::common::enums;
using schema::Field;
using schema::FieldKind;
using schema::Scalar;

std::string boolstr(bool v) { return v ? "true" : "false"; }
// enum value -> name, else the raw number (round-trips: numbers parse back). Via the one enum codec
// (enums::enumName), so this and the decompiler's revNum are one rule — and deterministic on aliases
// (B5), unlike the old arbitrary unordered_map scan.
std::string revNum(const std::unordered_map<std::string, long>& t, long v) {
    std::string nm = en::enumName(v, t);
    return nm.empty() ? std::to_string(v) : nm;
}

// enum type name -> table. Extended as categories need more; the decompiler references these by the
// same names (en::Role() etc.).
/// @note Resolves through the schema-generated tables (en::EnumTable, baked from schema/metadata/enums),
///       so any enum yaml renders/parses by annotation alone — no hand-kept list to fall out of date.
const std::unordered_map<std::string, long>* enumTable(const std::string& n) {
    const auto& t = en::EnumTable(n);
    return t.empty() ? nullptr : &t;
}

// Decode a scalar field's raw little-endian bytes to a signed long (sign-extended by type).
// Returns std::int64_t (NOT long — long is 32-bit on MSVC, which truncated 8-byte fields like
// hkbNode::userData / any Int64/UInt64; BR review #6).
std::int64_t decodeInt(const std::vector<std::uint8_t>& raw, Scalar s) {
    const int w = schema::ScalarWidth(s);
    std::uint64_t u = 0;
    for (int i = 0; i < w && i < static_cast<int>(raw.size()); ++i)
        u |= static_cast<std::uint64_t>(raw[i]) << (8 * i);
    switch (s) {
        case Scalar::Int8:  return static_cast<std::int8_t>(u);
        case Scalar::Int16: return static_cast<std::int16_t>(u);
        case Scalar::Int32: return static_cast<std::int32_t>(u);
        case Scalar::Int64: return static_cast<std::int64_t>(u);
        default:            return static_cast<std::int64_t>(u);   // unsigned kinds (full 64-bit)
    }
}
float decodeFloat(const std::vector<std::uint8_t>& raw) {
    float f = 0.f;
    if (raw.size() >= 4) std::memcpy(&f, raw.data(), 4);
    return f;
}

std::string renderScalar(const Field& f, const io::FieldValue& v) {
    if (f.scalar == Scalar::Float) return text::FormatFloat(decodeFloat(v.raw));
    if (f.scalar == Scalar::Bool)  return boolstr(!v.raw.empty() && v.raw[0] != 0);
    const std::int64_t iv = decodeInt(v.raw, f.scalar);   // int64: renders userData / any 64-bit field (#6)
    if (!f.enumName.empty()) {
        const auto* t = enumTable(f.enumName);
        if (t) return f.isFlags ? en::FormatFlags(iv, *t) : revNum(*t, iv);
    }
    return std::to_string(iv);
}

// ── name resolution (from the graph's hkbBehaviorGraphStringData) ─────────────
struct NameResolver {
    std::vector<std::string> events, variables, attributes, charProps;
    // The one name<->index codec, built once per roster (decode = index->name, encode = the dup-guard).
    // Bones/events/vars match EXACTLY (Havok is case-sensitive) — the linker never folds.
    CB::engine::linker::Linker eventsL, variablesL, charPropsL;
    void index() {   // call after the roster vectors are filled
        namespace linker = CB::engine::linker;
        eventsL    = linker::Linker::OfOrderedNames(events);
        variablesL = linker::Linker::OfOrderedNames(variables);
        charPropsL = linker::Linker::OfOrderedNames(charProps);
    }
    // index -> name (decode). "" out of range (caller keeps the raw number) — byte-identical to the
    // former bounds-checked vector read (decode(i) == roster[i]).
    std::string variable(long i) const { return variablesL.decode(i).value_or(std::string{}); }
    // Merge-safe name: only if `i` is the FIRST index carrying that name (a duplicate would remap on
    // round-trip, so the decompiler falls back to the raw id). encode() returns the first index for a
    // name, so "first occurrence" is exactly encode(name) == i — byte-identical to the old scan.
    std::string safe(const CB::engine::linker::Linker& L, long i) const {
        const auto nm = L.decode(i);
        if (!nm || nm->empty()) return {};
        const auto first = L.encode(*nm);
        return (first && *first == static_cast<CB::engine::linker::Value>(i)) ? *nm : std::string{};
    }
    std::string safeEvent(long i) const    { return safe(eventsL, i); }
    std::string safeVariable(long i) const { return safe(variablesL, i); }
};
NameResolver buildResolver(const Identity& identity) {
    NameResolver nr;
    // The canonical roster is the string-data the graph's hkbBehaviorGraphData points at
    // (merged graphs carry several hkbBehaviorGraphStringData; the first in object order is
    // not necessarily the graph's own). Follow the pointer; fall back to first-in-list.
    const io::SchemaObject* target = nullptr;
    for (const auto& [obj, cat] : identity.category) {
        if (std::string(obj->ClassName()) != "hkbBehaviorGraphData") continue;
        const auto* gd = dynamic_cast<const io::SchemaObject*>(obj);
        if (!gd) continue;
        target = gd->Child("stringData");
        break;
    }
    for (const auto& [obj, cat] : identity.category) {
        if (std::string(obj->ClassName()) != "hkbBehaviorGraphStringData") continue;
        const auto* so = dynamic_cast<const io::SchemaObject*>(obj);
        if (!so) continue;
        if (target && so != target) continue;   // prefer the graph-data's own string-data
        if (const io::FieldValue* v = so->FindField("eventNames"))             nr.events     = v->strs;
        if (const io::FieldValue* v = so->FindField("variableNames"))          nr.variables  = v->strs;
        if (const io::FieldValue* v = so->FindField("attributeNames"))         nr.attributes = v->strs;
        if (const io::FieldValue* v = so->FindField("characterPropertyNames")) nr.charProps  = v->strs;
        break;   // one string-data table per graph
    }
    nr.index();   // build the name<->index codecs from the parsed rosters
    return nr;
}

/// @return The referenced node's canonical id, or "" if null / not identified.
std::string refIdOf(const std::shared_ptr<IHavokObject>& p, const Identity& id) {
    if (!p) return {};
    auto it = id.ids.find(p.get());
    return it == id.ids.end() ? std::string() : it->second;
}
// A FormId "idx:local" is a valid id STRING (and the identity keyOf reads) but NOT a valid Windows
// FILENAME — ':' is reserved. The on-disk node filename swaps ':' -> '-'; the `id:` field content keeps
// the colon. Bijective + reversible; bare ids and Nemesis "code$N" contain no colon, so unaffected.
static std::string fsId(const std::string& id) {
    std::string s = id;
    for (char& c : s) if (c == ':') c = '-';
    return s;
}

// Canonical id key from a raw tagfile token (with or without a leading '#'). A numeric id normalizes to
// its unpadded decimal form ("0184" -> "184") so base names and refs match regardless of zero-padding; a
// Nemesis new-node id ("CASSIE$0") is kept verbatim. This is THE merge/emit key.
bool allDigits(const std::string& s) {
    return !s.empty() && std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isdigit(c) != 0; });
}
std::string canonId(const std::string& tok) {
    const std::string s = (!tok.empty() && tok[0] == '#') ? tok.substr(1) : tok;
    return allDigits(s) ? std::to_string(std::strtol(s.c_str(), nullptr, 10)) : s;
}
} // namespace

namespace {
/// @note Reflective emit, MEASUREMENT stage: every schema field in schema order, rendered by FieldKind + schema
///       annotations alone. File nodes (non-empty category) render as their id; inline objects nest as maps.
///       Lossless elisions: a nested object states `class:` only when it differs from the field's declared class
///       (a subclass); a null pointer or an empty array is omitted (absent == null / empty on parse); a `boneref`
///       of -1 ("no bone") is omitted (absent == -1).
/// @brief A skeleton unit's view: the role skeletons (`animation` = the animation container's skeletons[0], `ragdoll` =
///        the one the ragdoll instance names) become folders of name-keyed bone files, and `boneof` indices render as
///        bone names of the skeleton they index.
struct SkeletonView {
    std::unordered_map<const IHavokObject*, std::vector<std::string>> bones;   ///< every hkaSkeleton -> its bone names, in order
    std::unordered_map<const IHavokObject*, std::string>             role;    ///< the role skeletons -> their role
    const io::SchemaObject* ragdollInstance = nullptr;   ///< set when its boneToRigidBodyMap folds onto the ragdoll bones
};

/// @brief A bindable's character-property bindings folded into its `charprop` members: member path -> property name,
///        and the binding objects the bindings roster then leaves out.
struct CharPropFold {
    std::map<std::string, std::string>      byPath;
    std::unordered_set<const IHavokObject*> bindings;
};

struct ReflectCtx {
    const Identity&                 id;
    const NameResolver&             nr;
    const schema::SchemaRegistry&   reg;
    const std::vector<std::string>* bones;   ///< the graph's rig, in rig order; null = unknown (indices stay numeric)
    const std::unordered_map<const IHavokObject*, std::string>* cat;   ///< effective category (file nodes + shared/)
    std::uint16_t                   keyOwner = CB::engine::codec::formid::SELF_INDEX;   ///< master index of positional sub-record keys
    std::vector<std::pair<std::string, std::string>>* sink = nullptr;   ///< node-level rosters -> (field path, entries)
    std::string                     path;                            ///< dotted field path from the node to this object
    const SkeletonView*             skel = nullptr;                  ///< null outside a skeleton unit
    std::map<std::string, const std::vector<std::string>*> boneOf;  ///< `boneof:` field name -> that skeleton's bones
    const io::SchemaObject*         bindable = nullptr;              ///< the hkbBindable whose member paths `member` continues
    std::string                     member;                          ///< member path prefix from `bindable` ("bones:3/")
    std::shared_ptr<const CharPropFold> fold;                        ///< `bindable`'s folded bindings
    const CharPropFold*             setFold = nullptr;               ///< set while rendering `bindable`'s own binding set
};

/// @brief Whether the skeleton view writes `so`'s field `f` somewhere else: a role skeleton's bone arrays (its bones/
///        folder), or the folded boneToRigidBodyMap (the ragdoll bones' `rigidBody`).
bool skeletonViewTakes(const io::SchemaObject& so, const Field& f, const ReflectCtx& c) {
    if (!c.skel) return false;
    if (c.skel->role.count(&so)) return f.name == "bones" || f.name == "parentIndices" || f.name == "referencePose";
    return &so == c.skel->ragdollInstance && f.name == "boneToRigidBodyMap";
}

/// @brief The context for an inline object reached through field `f` (its rosters still belong to the node's folder).
ReflectCtx subCtx(const ReflectCtx& c, const std::string& f) { ReflectCtx s = c; if (s.sink) s.path += f + "."; return s; }
/// @brief The context for list ENTRIES: rosters nested in an entry stay inline in that entry.
ReflectCtx inlCtx(const ReflectCtx& c) { ReflectCtx s = c; s.sink = nullptr; s.path.clear(); return s; }

std::string reflectObject(const io::SchemaObject& so, const ReflectCtx& c, const std::string& declaredClass = {});

/// @note A float renders as %.9g (exact for float32) — except NaN, whose PAYLOAD bits the text form can't carry
///       (vanilla stores e.g. 0xFFC00000 in unused vector lanes); NaN renders as `nan_<8 hex bits>`, bit-exact.
std::string rfloat(const std::uint8_t* p) {
    float f; std::memcpy(&f, p, 4);
    if (f != f) { std::uint32_t u; std::memcpy(&u, p, 4); char b[24]; std::snprintf(b, sizeof b, "nan_%08x", u); return b; }
    return text::FormatFloat(f);
}
std::string rfloat(const std::vector<std::uint8_t>& raw) {
    std::uint8_t b[4] = { 0, 0, 0, 0 };
    std::memcpy(b, raw.data(), std::min<std::size_t>(4, raw.size()));
    return rfloat(b);
}
std::string rvec(const std::vector<std::uint8_t>& raw) {
    std::uint8_t b[16] = {};
    std::memcpy(b, raw.data(), std::min<std::size_t>(16, raw.size()));
    return "'(" + rfloat(b) + " " + rfloat(b + 4) + " " + rfloat(b + 8) + " " + rfloat(b + 12) + ")'";
}

std::string indentLines(const std::string& block, const std::string& first, const std::string& rest) {
    std::string out;
    std::size_t s = 0;
    bool firstLine = true;
    while (s < block.size()) {
        const std::size_t e = block.find('\n', s);
        const std::string line = block.substr(s, e == std::string::npos ? std::string::npos : e - s);
        out += (firstLine ? first : rest) + line + "\n";
        firstLine = false;
        if (e == std::string::npos) break;
        s = e + 1;
    }
    return out;
}

bool isFileNode(const IHavokObject* o, const ReflectCtx& c) {
    const auto it = c.cat->find(o);
    return it != c.cat->end() && !it->second.empty();
}

/// @brief The context for the object behind `owner`'s field `f` (element `k` of an array): an inline struct continues
///        the bindable's member path (`gains/`, `bones:3/`); a pointer leaves it (a member path never crosses one), and
///        the bindable's own binding set renders with its folded bindings left out.
ReflectCtx childCtx(const ReflectCtx& c, const io::SchemaObject& owner, const Field& f, std::size_t k = 0) {
    ReflectCtx s = c;
    s.setFold = nullptr;
    if (f.kind == FieldKind::Struct) s.member = c.member + f.name + "/";
    else if (f.kind == FieldKind::StructArray) s.member = c.member + f.name + ":" + std::to_string(k) + "/";
    else {
        s.bindable = nullptr;
        s.member.clear();
        s.fold.reset();
        if (f.name == "variableBindingSet" && &owner == c.bindable) s.setFold = c.fold.get();
    }
    return s;
}

/// @brief Every `charprop` member under `so` (through inline structs and struct arrays), in schema walk order: its
///        member path and whether it holds its neutral value (a pointer's target inline, not shared).
void charPropSites(const io::SchemaObject& so, const std::string& prefix, const ReflectCtx& c,
                   std::vector<std::pair<std::string, bool>>& out) {
    const auto fields = so.Fields();
    const auto& vals  = so.Values();
    for (std::size_t i = 0; i < fields.size() && i < vals.size(); ++i) {
        const Field& f = *fields[i];
        if (!f.charProp.empty()) {
            /// @note A null pointer holds no bone data, but `{ charprop }` parses back to an empty set: it never folds.
            const bool ptr = f.kind == FieldKind::Ptr;
            out.emplace_back(prefix + f.name, CharPropNeutral(f, vals[i]) && (!ptr || (vals[i].obj && !isFileNode(vals[i].obj.get(), c))));
            continue;
        }
        if (f.kind == FieldKind::Struct)
            if (const auto* e = dynamic_cast<const io::SchemaObject*>(vals[i].obj.get())) charPropSites(*e, prefix + f.name + "/", c, out);
        if (f.kind == FieldKind::StructArray)
            for (std::size_t k = 0; k < vals[i].objs.size(); ++k)
                if (const auto* e = dynamic_cast<const io::SchemaObject*>(vals[i].objs[k].get()))
                    charPropSites(*e, prefix + f.name + ":" + std::to_string(k) + "/", c, out);
    }
}

/// @brief Which of bindable `b`'s bindings fold into its `charprop` members: the trailing run of character-property
///        bindings (bitIndex -1) to members holding their neutral value, in the members' walk order — the order
///        ParseHky appends them back in. Anything else keeps the plain form.
std::shared_ptr<const CharPropFold> foldFor(const io::SchemaObject& b, const ReflectCtx& c) {
    auto out = std::make_shared<CharPropFold>();
    const auto* set = b.Child("variableBindingSet");
    if (!set) return out;
    const auto& binds = set->Children("bindings");
    std::vector<std::pair<std::string, bool>> sites;
    charPropSites(b, "", c, sites);
    if (sites.empty() || binds.empty()) return out;
    std::vector<std::pair<std::size_t, std::string>> picked;   ///< (site position, property), last binding first
    std::size_t s = binds.size();
    while (s > 0) {
        const auto* bo = dynamic_cast<const io::SchemaObject*>(binds[s - 1].get());
        if (!bo || bo->GetInt("bindingType") != 1 || bo->GetInt("bitIndex") != -1) break;
        const std::string& path = bo->Str("memberPath");
        const auto site = std::find_if(sites.begin(), sites.end(), [&](const auto& x) { return x.first == path; });
        if (site == sites.end() || !site->second) break;
        const std::string name = c.nr.safe(c.nr.charPropsL, static_cast<long>(bo->GetInt("variableIndex")));
        if (name.empty()) break;
        picked.emplace_back(static_cast<std::size_t>(site - sites.begin()), name);
        --s;
    }
    std::reverse(picked.begin(), picked.end());
    for (std::size_t k = 1; k < picked.size(); ++k)
        if (picked[k].first <= picked[k - 1].first) return out;
    for (std::size_t k = 0; k < picked.size(); ++k) {
        out->byPath[sites[picked[k].first].first] = picked[k].second;
        out->bindings.insert(binds[s + k].get());
    }
    return out;
}

/// @brief The folded form of `so`'s `charprop` field `f`, or empty when its binding does not fold.
std::string charPropText(const Field& f, const ReflectCtx& c) {
    if (f.charProp.empty() || !c.fold) return {};
    const auto it = c.fold->byPath.find(c.member + f.name);
    return it == c.fold->byPath.end() ? std::string() : f.name + ": { charprop: " + text::Quote(it->second) + " }\n";
}

std::vector<std::uint8_t> slice(const std::vector<std::uint8_t>& raw, std::size_t k, std::size_t w) {
    const std::size_t a = std::min(raw.size(), k * w), b = std::min(raw.size(), (k + 1) * w);
    return std::vector<std::uint8_t>(raw.begin() + a, raw.begin() + b);
}

/// @note Enums render through the REGISTRY (schema enum tree), not a hardcoded table. Flags decode UNSIGNED at the
///       field's width: sign-extending an int16 bitfield turns garbage high bits into 0xFFFF....
std::string scalarText(const Field& f, const std::vector<std::uint8_t>& raw, const schema::SchemaRegistry& reg) {
    if (f.scalar == Scalar::Half) {
        std::uint16_t h = 0; if (raw.size() >= 2) std::memcpy(&h, raw.data(), 2);
        char b[16]; std::snprintf(b, sizeof b, "0x%04x", h); return b;
    }
    if (f.scalar == Scalar::Float) return rfloat(raw);
    if (!f.enumName.empty())
        if (const schema::EnumDef* e = reg.FindEnum(f.enumName)) {
            std::unordered_map<std::string, long> t;
            for (const auto& it : e->items) t.emplace(it.name, it.value);
            if (f.isFlags || e->isFlags) {
                const int w = schema::ScalarWidth(f.scalar);
                std::uint64_t u = 0;
                for (int i = 0; i < w && i < static_cast<int>(raw.size()); ++i) u |= static_cast<std::uint64_t>(raw[i]) << (8 * i);
                return en::FormatFlags(static_cast<long>(u), t);
            }
            const std::int64_t iv = decodeInt(raw, f.scalar);
            const std::string nm = en::enumName(static_cast<long>(iv), t);
            return nm.empty() ? std::to_string(iv) : nm;
        }
    io::FieldValue v; v.raw = raw;
    return renderScalar(f, v);
}

/// @brief A bone index as its rig name; the raw number when the rig is unknown or the index is out of range.
std::string boneText(std::int64_t idx, const ReflectCtx& c) {
    if (c.bones && idx >= 0 && idx < static_cast<std::int64_t>(c.bones->size())) return text::Quote((*c.bones)[static_cast<std::size_t>(idx)]);
    return std::to_string(idx);
}

/// @brief A `boneof` index as a bone name of the skeleton it indexes; the raw number when that skeleton's bones are
///        unknown or the index is out of range.
std::string boneOfText(std::int64_t idx, const Field& f, const ReflectCtx& c) {
    const auto it = c.boneOf.find(f.boneOf);
    const std::vector<std::string>* names = it == c.boneOf.end() ? nullptr : it->second;
    if (names && idx >= 0 && idx < static_cast<std::int64_t>(names->size())) return text::Quote((*names)[static_cast<std::size_t>(idx)]);
    return std::to_string(idx);
}

/// @brief `boneweights`: a flat map bone-name -> weight over the rig. Reserved keys: `default:` (only when the
///        array's most common value differs from the schema default) and `count:` (only when the array length
///        differs from the rig). Unlisted bones take the default; compile sizes the array to the rig when no
///        count is given, so a bone added to the rig gets the default automatically.
/// @note  Values compare by their rendered text, so -0 stays distinct from 0 (bit-exact). `whole` states `default:` and
///        `count:` always — in a PATCH a map without them is a per-bone override, so a whole value must say so.
std::string boneWeightsText(const Field& f, const std::vector<std::uint8_t>& raw, const ReflectCtx& c, bool whole = false) {
    const std::size_t n = raw.size() / 4;
    std::vector<std::string> vals;
    for (std::size_t k = 0; k < n; ++k) vals.push_back(rfloat(slice(raw, k, 4)));
    if (!c.bones || n > c.bones->size()) {   // no rig / array longer than the rig: raw list, still lossless
        std::string y = f.name + ": [";
        for (std::size_t k = 0; k < n; ++k) y += (k ? ", " : "") + vals[k];
        return y + "]\n";
    }
    std::map<std::string, std::size_t> freq;
    for (const auto& v : vals) ++freq[v];
    std::string dflt = f.boneWeightDefault;
    std::size_t best = freq.count(dflt) ? freq[dflt] : 0;
    for (const auto& [v, k] : freq) if (k > best) { best = k; dflt = v; }
    std::string body;
    if (whole || dflt != f.boneWeightDefault) body += "  default: " + dflt + "\n";
    if (whole || n != c.bones->size())        body += "  count: " + std::to_string(n) + "\n";
    for (std::size_t k = 0; k < n; ++k)
        if (vals[k] != dflt) body += "  " + text::Quote((*c.bones)[k]) + ": " + vals[k] + "\n";
    return body.empty() ? f.name + ": {}\n" : f.name + ":\n" + body;
}

/// @brief `unwrap:` collapse — when an inline object's rendered body is exactly its unwrap field (no `class:` line,
///        no other top-level key), return that field's rendering re-keyed as `key`; else empty (keep the full form).
std::string unwrapped(const io::SchemaObject& so, const std::string& body, const std::string& key) {
    const std::string& u = so.Schema()->unwrap;
    if (u.empty() || body.rfind(u + ":", 0) != 0 || body == u + ": {}\n") return {};   // `{}` stays an empty object
    for (std::size_t s = body.find('\n'); s != std::string::npos && s + 1 < body.size(); s = body.find('\n', s + 1))
        if (body[s + 1] != ' ') return {};   // a second top-level key: other fields are present
    return key + body.substr(u.size());
}

std::string elemText(const std::shared_ptr<IHavokObject>& p, const ReflectCtx& c, const std::string& key, const std::string& declared) {
    if (!p) return {};
    if (isFileNode(p.get(), c)) return key + ": " + refIdOf(p, c.id) + "\n";
    const auto* so = dynamic_cast<const io::SchemaObject*>(p.get());
    if (!so) return key + ": '?'\n";
    const std::string body = reflectObject(*so, subCtx(c, key), declared);
    if (const std::string u = unwrapped(*so, body, key); !u.empty()) return u;
    return body.empty() ? key + ": {}\n" : key + ":\n" + indentLines(body, "  ", "  ");   // {} != null on parse
}

std::string listText(const std::vector<std::shared_ptr<IHavokObject>>& objs, const ReflectCtx& c,
                     const std::string& key, const std::string& declared,
                     const io::SchemaObject* owner = nullptr, const Field* field = nullptr) {
    if (objs.empty()) return {};
    std::string y = key + ":\n";
    for (std::size_t k = 0; k < objs.size(); ++k) {
        const auto& p = objs[k];
        if (!p) { y += "  - null\n"; continue; }
        if (isFileNode(p.get(), c)) { y += "  - " + refIdOf(p, c.id) + "\n"; continue; }
        const auto* so = dynamic_cast<const io::SchemaObject*>(p.get());
        if (!so) { y += "  - '?'\n"; continue; }
        const std::string body = reflectObject(*so, inlCtx(owner && field ? childCtx(c, *owner, *field, k) : c), declared);
        y += body.empty() ? std::string("  - {}\n") : indentLines(body, "  - ", "    ");
    }
    return y;
}

template <class F>
std::string flowList(std::size_t n, F&& at) {
    std::string y = "[";
    for (std::size_t i = 0; i < n; ++i) y += (i ? ", " : "") + at(i);
    return y + "]";
}

/// @note VOCABULARY VIEW (`vocab: true`, hkbBehaviorGraphData): the graph's variables / character properties /
///       events / attributes render as NAME-KEYED RECORDS instead of Havok's parallel arrays (names in the string
///       data, infos, word values, quad values). Position is meaningless for vocabulary and names are identity, so
///       mods add or change entries by name. Lossless under invariants that hold for every vanilla graph (parallel
///       lengths, unique names, empty min/max, quad/variant indices running in variable order); an object that
///       breaks one keeps the raw field form.
const std::vector<std::string>* strsOf(const io::SchemaObject* so, const char* field) {
    const io::FieldValue* v = so ? so->FindField(field) : nullptr;
    return v ? &v->strs : nullptr;
}
/// @brief VECTOR3 / VECTOR4 / QUATERNION variables store their value in the quad array (word = quad index).
bool isQuadType(long t) { return t == 6 || t == 7 || t == 8; }

/// @brief A VALUED name roster: names in the owner's `stringData`, infos on the owner, initial values in a value set
///        (word values parallel to the names; vector/quaternion values in its quad array, the word holding the index).
///        Graph `variables` and character `characterProperties` are the same shape.
struct ValuedRoster { const char* key; const char* names; const char* infos; const char* values; };
constexpr ValuedRoster kGraphVariables{ "variables", "variableNames", "variableInfos", "variableInitialValues" };
constexpr ValuedRoster kCharacterProperties{ "characterProperties", "characterPropertyNames", "characterPropertyInfos", "characterPropertyValues" };

bool uniqueNames(const std::vector<std::string>& n) { return std::set<std::string>(n.begin(), n.end()).size() == n.size(); }

/// @brief POINTER variables (character bone-weight properties) store their value in the variant array (word = index).
bool isPointerType(long t) { return t == 5; }

/// @brief The valued roster renders losslessly as records: names / infos / words in lockstep, unique names, and the
///        quad / variant indices running in roster order over the whole quad / variant array.
bool valuedRenderable(const io::SchemaObject& owner, const ValuedRoster& r, bool lenient = false) {
    const auto* sd = owner.Child("stringData");
    const auto* vs = owner.Child(r.values);
    if (!sd || !vs) return false;
    const auto* names = strsOf(sd, r.names);
    const auto& infos = owner.Children(r.infos);
    const auto& words = vs->Children("wordVariableValues");
    const io::FieldValue* quads = vs->FindField("quadVariableValues");
    const auto& variants = vs->Children("variantVariableValues");
    if (!names || !quads) return false;
    if (names->size() != infos.size() || names->size() != words.size() || (!lenient && !uniqueNames(*names))) return false;
    std::size_t q = 0, p = 0;
    for (std::size_t i = 0; i < infos.size(); ++i) {
        const auto* info = dynamic_cast<const io::SchemaObject*>(infos[i].get());
        const auto* word = dynamic_cast<const io::SchemaObject*>(words[i].get());
        if (!info || !word) return false;
        const long t = static_cast<long>(info->GetInt("type"));
        if (isPointerType(t)) { if (word->GetInt("value") != static_cast<long>(p)) return false; ++p; }
        else if (isQuadType(t)) { if (word->GetInt("value") != static_cast<long>(q)) return false; ++q; }
    }
    return q * 16 == quads->raw.size() && p == variants.size();
}

/// @brief `lenient` (derive's MOD side only) allows duplicate names: a mod that renames an entry onto a name the roster
///        already has (Precision renames vanilla's ' iState_NPCSneaking' onto 'iState_NPCSneaking') still diffs by
///        name — names are identity and rosters only union, so the rename drops out (first occurrence wins).
bool vocabRenderable(const io::SchemaObject& so, bool lenient = false) {
    if (so.Schema()->vocab == "character") return valuedRenderable(so, kCharacterProperties, lenient);
    if (so.Schema()->vocab != "graph" || !valuedRenderable(so, kGraphVariables, lenient)) return false;
    const auto* sd = so.Child("stringData");
    const auto* en = strsOf(sd, "eventNames");
    const auto* cn = strsOf(sd, "characterPropertyNames");
    const auto* an = strsOf(sd, "attributeNames");
    const auto& ei = so.Children("eventInfos");
    const auto& ci = so.Children("characterPropertyInfos");
    const io::FieldValue* ad = so.FindField("attributeDefaults");
    if (!en || !cn || !an || !ad) return false;
    if (en->size() != ei.size() || cn->size() != ci.size() || an->size() * 4 != ad->raw.size()) return false;
    if (!so.Children("wordMinVariableValues").empty() || !so.Children("wordMaxVariableValues").empty()) return false;
    return lenient || (uniqueNames(*en) && uniqueNames(*cn) && uniqueNames(*an));
}

std::string enumText(const char* table, long v, bool flags) {
    const auto& t = en::EnumTable(table);
    if (flags) return en::FormatFlags(v, t);
    const std::string n = en::enumName(v, t);
    return n.empty() ? std::to_string(v) : n;
}

/// @brief One variable / character-property record. Flow YAML (`{type: …, value: …}`) for word and quad values; a
///        block for a POINTER value, whose `value:` is the pointed-to object (a bone-weight set: its bone-name map) or
///        a node id when shared. `quad` / `variant` = the value's slot (their types only).
/// @note `full` states role + flags even at 0: a PATCH record's absent key means "unchanged", so a diff that
///       clears one must say so.
std::string infoRecord(const io::SchemaObject& info, const io::SchemaObject* word, const std::uint8_t* quad,
                       const std::shared_ptr<IHavokObject>* variant, bool full, const ReflectCtx& c) {
    const long t = static_cast<long>(info.GetInt("type"));
    std::vector<std::pair<std::string, std::string>> kv{ { "type", enumText("VariableType", t, false) } };
    if (const auto* role = info.Child("role")) {
        const long r = static_cast<long>(role->GetInt("role"));
        const long fl = static_cast<long>(static_cast<std::uint16_t>(role->GetInt("flags")));
        if (r || full)  kv.push_back({ "role", enumText("Role", r, false) });
        if (fl || full) kv.push_back({ "flags", enumText("RoleFlags", fl, true) });
    }
    if (word && variant) {
        const auto& p = *variant;
        const auto* so = dynamic_cast<const io::SchemaObject*>(p.get());
        std::string y;
        for (const auto& [k, v] : kv) y += k + ": " + v + "\n";
        if (!p) return y + "value: null\n";
        if (isFileNode(p.get(), c)) return y + "value: " + refIdOf(p, c.id) + "\n";
        const std::string body = so ? reflectObject(*so, inlCtx(c), "hkReferencedObject") : std::string();
        return y + (body.empty() ? std::string("value: {}\n") : "value:\n" + indentLines(body, "  ", "  "));
    }
    if (word) {
        const io::FieldValue* wv = word->FindField("value");
        std::string val;
        if (quad) val = rvec(std::vector<std::uint8_t>(quad, quad + 16));
        else if (t == 4 && wv) val = rfloat(wv->raw);
        else {
            const long w = static_cast<long>(word->GetInt("value"));
            val = (t == 0 && (w == 0 || w == 1)) ? boolstr(w != 0) : std::to_string(w);
        }
        kv.push_back({ "value", val });
    }
    std::string y = "{";
    for (std::size_t k = 0; k < kv.size(); ++k) y += (k ? ", " : "") + kv[k].first + ": " + kv[k].second;
    return y + "}";
}

/// @brief A valued roster entry's info and (pointer types) its value object, by roster position.
const io::SchemaObject* valuedInfo(const io::SchemaObject& owner, const ValuedRoster& r, std::size_t pos) {
    const auto& infos = owner.Children(r.infos);
    return pos < infos.size() ? dynamic_cast<const io::SchemaObject*>(infos[pos].get()) : nullptr;
}
std::shared_ptr<IHavokObject> valuedPointer(const io::SchemaObject& owner, const ValuedRoster& r, std::size_t pos) {
    const auto* info = valuedInfo(owner, r, pos);
    const auto* vs = owner.Child(r.values);
    if (!info || !vs || !isPointerType(static_cast<long>(info->GetInt("type")))) return nullptr;
    const auto& words = vs->Children("wordVariableValues");
    const auto& variants = vs->Children("variantVariableValues");
    const auto* word = pos < words.size() ? dynamic_cast<const io::SchemaObject*>(words[pos].get()) : nullptr;
    const long vi = word ? static_cast<long>(word->GetInt("value")) : -1;
    return vi >= 0 && static_cast<std::size_t>(vi) < variants.size() ? variants[static_cast<std::size_t>(vi)] : nullptr;
}

/// @brief Every record of the vocabulary, per roster, in roster order: (roster key, name, record text).
void valuedRecords(const io::SchemaObject& owner, const ValuedRoster& r, bool full, const ReflectCtx& c,
                   std::vector<std::tuple<std::string, std::string, std::string>>& out) {
    const auto* vs = owner.Child(r.values);
    const auto& names = *strsOf(owner.Child("stringData"), r.names);
    const auto& infos = owner.Children(r.infos);
    const auto& words = vs->Children("wordVariableValues");
    const auto& quads = vs->FindField("quadVariableValues")->raw;
    const auto& variants = vs->Children("variantVariableValues");
    std::size_t q = 0, p = 0;
    for (std::size_t i = 0; i < names.size(); ++i) {
        const auto& info = *dynamic_cast<const io::SchemaObject*>(infos[i].get());
        const long t = static_cast<long>(info.GetInt("type"));
        const std::uint8_t* quad = isQuadType(t) ? quads.data() + 16 * q++ : nullptr;
        const std::shared_ptr<IHavokObject>* variant = isPointerType(t) ? &variants[p++] : nullptr;
        out.emplace_back(r.key, names[i], infoRecord(info, dynamic_cast<const io::SchemaObject*>(words[i].get()), quad, variant, full, c));
    }
}

std::vector<std::tuple<std::string, std::string, std::string>> vocabRecords(const io::SchemaObject& gd, const ReflectCtx& c,
                                                                            bool full = false) {
    std::vector<std::tuple<std::string, std::string, std::string>> out;
    if (gd.Schema()->vocab == "character") { valuedRecords(gd, kCharacterProperties, full, c, out); return out; }
    valuedRecords(gd, kGraphVariables, full, c, out);
    const auto* sd = gd.Child("stringData");
    const auto& cn = *strsOf(sd, "characterPropertyNames");
    const auto& ci = gd.Children("characterPropertyInfos");
    for (std::size_t i = 0; i < cn.size(); ++i)
        out.emplace_back("characterProperties", cn[i], infoRecord(*dynamic_cast<const io::SchemaObject*>(ci[i].get()), nullptr, nullptr, nullptr, full, c));
    const auto& enm = *strsOf(sd, "eventNames");
    const auto& ei = gd.Children("eventInfos");
    for (std::size_t i = 0; i < enm.size(); ++i) {
        const long fl = static_cast<long>(static_cast<std::uint32_t>(dynamic_cast<const io::SchemaObject*>(ei[i].get())->GetInt("flags")));
        out.emplace_back("events", enm[i], fl || full ? "{flags: " + enumText("EventInfoFlags", fl, true) + "}" : std::string("{}"));
    }
    const auto& an = *strsOf(sd, "attributeNames");
    const auto& ad = gd.FindField("attributeDefaults")->raw;
    for (std::size_t i = 0; i < an.size(); ++i) out.emplace_back("attributes", an[i], rfloat(ad.data() + 4 * i));
    return out;
}

/// @brief The vocabulary's record keys, in render order.
std::vector<const char*> vocabKeys(const io::SchemaObject& so) {
    if (so.Schema()->vocab == "character") return { kCharacterProperties.key };
    return { "variables", "characterProperties", "events", "attributes" };
}

/// @brief One vocabulary record line(s) at indent 2: flow records inline, block records (pointer values) nested.
std::string vocabLine(const std::string& name, const std::string& rec) {
    return rec.find('\n') == std::string::npos ? "  " + text::Quote(name) + ": " + rec + "\n"
                                               : "  " + text::Quote(name) + ":\n" + indentLines(rec, "    ", "    ");
}

std::string renderVocab(const io::SchemaObject& gd, const ReflectCtx& c) {
    std::map<std::string, std::string> blocks;
    for (const char* k : vocabKeys(gd)) blocks[k];
    for (const auto& [k, name, rec] : vocabRecords(gd, c)) blocks[k] += vocabLine(name, rec);
    std::string y;
    for (const char* k : vocabKeys(gd))
        y += blocks[k].empty() ? std::string(k) + ": {}\n" : std::string(k) + ":\n" + blocks[k];
    return y;
}

/// @brief True when field `name` of `so` is carried by the vocabulary view instead of its own rendering: the valued
///        roster's infos and value set (its names are left out of `stringData`).
bool vocabField(const io::SchemaObject& so, const std::string& name) {
    return so.Schema()->vocab == "character" && (name == kCharacterProperties.infos || name == kCharacterProperties.values);
}

/// @brief True when `so`'s `<field>=<value>` condition (a schema `charpropif`) holds.
bool condTrue(const io::SchemaObject& so, const std::string& cond) {
    const auto eq = cond.find('=');
    if (cond.empty() || eq == std::string::npos) return false;
    const std::string field = cond.substr(0, eq);
    return (so.HasField(field) ? static_cast<long>(so.GetInt(field)) : 0L) == std::strtol(cond.c_str() + eq + 1, nullptr, 10);
}

/// @brief One field's rendering (possibly empty = absent), exactly as reflectObject writes it — the unit of both the
///        emit and the field-patch diff.
std::string renderField(const io::SchemaObject& so, std::size_t i, const ReflectCtx& c);

/// @brief A roster key's parts: `ref`, `id`, `slot`, or `field|bone` with the element field it reads.
std::pair<std::string, std::string> rosterKind(const Field& f) {
    const auto c = f.roster.find(':');
    return c == std::string::npos ? std::pair{ f.roster, std::string() } : std::pair{ f.roster.substr(0, c), f.roster.substr(c + 1) };
}

/// @brief The positional sub-record key for list position `k`: the unit's own (self) unless `owner` names a master.
std::string mintedKey(std::size_t k, std::uint16_t owner = CB::engine::codec::formid::SELF_INDEX) {
    const auto f = CB::engine::codec::formid::make(owner, static_cast<std::uint32_t>(k));
    return f ? CB::engine::codec::formid::format(*f) : std::to_string(k);
}

/// @brief A `field:`/`bone:` roster element's natural key and the index of the field it comes from (empty key = none).
std::pair<std::string, std::size_t> naturalKey(const io::SchemaObject& e, const std::string& kind, const std::string& keyField,
                                               const ReflectCtx& c) {
    const auto ef = e.Fields();
    for (std::size_t j = 0; j < ef.size(); ++j) {
        if (ef[j]->name != keyField) continue;
        if (kind == "field") return { e.Values()[j].str, j };
        const std::int64_t bi = decodeInt(e.Values()[j].raw, ef[j]->scalar);
        if (!c.bones || bi < 0 || bi >= static_cast<std::int64_t>(c.bones->size())) return { std::string(), j };
        return { (*c.bones)[static_cast<std::size_t>(bi)], j };
    }
    return { std::string(), ef.size() };
}

/// @brief One roster entry line(s) at indent 0: `'key': value` — `{}` for empty, inline for a scalar (a ref / null),
///        a nested block otherwise.
std::string rosterEntry(const std::string& key, const std::string& v) {
    const bool block = v.find('\n') != std::string::npos;
    return text::Quote(key) + ":" + (v.empty() ? std::string(" {}\n") : block ? "\n" + indentLines(v, "  ", "  ") : " " + v + "\n");
}

/// @brief A `roster:` array's KEYED entries (`'key': body`, indent 0), in list order. Keys: the child node's id
///        (`ref`), an element string field (`field:`), the bone an element's index field names (`bone:`), or the minted
///        sub-record FormID (`id`/`slot`, from Identity::subKeys, else by position). The key field is not repeated in
///        the body; rosters nested inside an entry render inline in it.
/// @return nullopt when this list can't be keyed losslessly (a null / inline child in a `ref` roster, a duplicate
///         key, an unknown bone) — the caller keeps the plain list.
std::optional<std::string> rosterEntries(const io::SchemaObject& so, std::size_t i, const ReflectCtx& c,
                                         const std::vector<std::shared_ptr<IHavokObject>>* only = nullptr) {
    const Field& f = *so.Fields()[i];
    const auto& objs = only ? *only : so.Values()[i].objs;
    const auto [kind, keyField] = rosterKind(f);
    const std::vector<std::string>* keys = nullptr;
    if (auto it = c.id.subKeys.find({ &so, i }); it != c.id.subKeys.end() && it->second.size() == objs.size()) keys = &it->second;
    std::set<std::string> seen;
    std::string y;
    for (std::size_t k = 0; k < objs.size(); ++k) {
        const auto& p = objs[k];
        const auto* e = p ? dynamic_cast<const io::SchemaObject*>(p.get()) : nullptr;
        const ReflectCtx ci = inlCtx(childCtx(c, so, f, k));
        std::string key, body;
        if (kind == "ref") {
            if (!p || !isFileNode(p.get(), c)) return std::nullopt;
            key = refIdOf(p, c.id);
        } else if (kind == "id" || kind == "slot") {
            key = keys ? (*keys)[k] : mintedKey(k, c.keyOwner);
            if (!p) body = "null";
            else if (isFileNode(p.get(), c)) body = refIdOf(p, c.id);
            else if (e) body = reflectObject(*e, ci, f.ref);
            else return std::nullopt;
        } else if ((kind == "field" || kind == "bone") && e && !isFileNode(p.get(), c) && e->ClassName() == f.ref) {
            std::size_t kf = 0;
            std::tie(key, kf) = naturalKey(*e, kind, keyField, c);
            if (kf == e->Fields().size() || (kind == "bone" && key.empty())) return std::nullopt;
            for (std::size_t j = 0; j < e->Fields().size(); ++j) if (j != kf) body += renderField(*e, j, ci);
        } else {
            return std::nullopt;
        }
        if (!seen.insert(key).second) return std::nullopt;
        y += rosterEntry(key, body);
    }
    return y;
}

std::string renderField(const io::SchemaObject& so, std::size_t i, const ReflectCtx& c) {
    const auto fields = so.Fields();
    const auto& vals  = so.Values();
    if (i >= fields.size() || i >= vals.size()) return {};
    std::string y;
        const Field& f = *fields[i];
        const io::FieldValue& v = vals[i];
        if (f.name.empty() || (f.ignored && !f.hkyEmit)) return y;
        switch (f.kind) {
            case FieldKind::Scalar: {
                const std::size_t w = static_cast<std::size_t>(schema::ScalarWidth(f.scalar));
                const std::size_t n = w ? v.raw.size() / w : 0;
                if (const std::string cp = n <= 1 ? charPropText(f, c) : std::string(); !cp.empty()) {
                    y += cp;
                } else if (f.user && n <= 1 && decodeInt(v.raw, f.scalar) == f.userValue) {
                    y += f.name + ": user\n";
                } else if (!f.boneOf.empty() && n <= 1) {
                    y += f.name + ": " + boneOfText(decodeInt(v.raw, f.scalar), f, c) + "\n";
                } else if (f.boneRef && n <= 1) {
                    const std::int64_t iv = decodeInt(v.raw, f.scalar);
                    if (iv != -1) y += f.name + ": " + boneText(iv, c) + "\n";
                } else if (n <= 1) {
                    y += f.name + ": " + scalarText(f, v.raw, c.reg) + "\n";
                    const long iv = static_cast<long>(decodeInt(v.raw, f.scalar));
                    if (!f.eventRef.empty()) { const std::string s = c.nr.safeEvent(iv);    if (!s.empty()) y += f.eventRef + ": " + text::Quote(s) + "\n"; }
                    if (!f.varRef.empty()) {
                        const std::string s = condTrue(so, f.charPropIf) ? c.nr.safe(c.nr.charPropsL, iv) : c.nr.safeVariable(iv);
                        if (!s.empty()) y += f.varRef + ": " + text::Quote(s) + "\n";
                    }
                } else {
                    y += f.name + ": " + flowList(n, [&](std::size_t k) { return scalarText(f, slice(v.raw, k, w), c.reg); }) + "\n";
                }
                break;
            }
            case FieldKind::String:
            case FieldKind::CString:    y += f.name + ": " + text::Quote(v.str) + "\n"; break;
            /// @note A `count:` field (hkTransform's four columns, a motor atom's three motors) renders as a list of its
            ///       elements; a single element keeps the bare form.
            case FieldKind::Vector4:
            case FieldKind::Quaternion:
                if (v.raw.size() <= 16) y += f.name + ": " + rvec(v.raw) + "\n";
                else y += f.name + ": " + flowList(v.raw.size() / 16, [&](std::size_t k) { return rvec(slice(v.raw, k, 16)); }) + "\n";
                break;
            case FieldKind::QsTransform: {
                const auto qs = [&](std::size_t k) { return flowList(3, [&](std::size_t j) { return rvec(slice(v.raw, k * 3 + j, 16)); }); };
                if (v.raw.size() <= 48) y += f.name + ": " + qs(0) + "\n";
                else y += f.name + ": " + flowList(v.raw.size() / 48, qs) + "\n";
                break;
            }
            case FieldKind::Ptr:
                if (f.count > 1) {
                    if (std::any_of(v.objs.begin(), v.objs.end(), [](const auto& o) { return o != nullptr; }))
                        y += listText(v.objs, childCtx(c, so, f), f.name, f.ref);
                    break;
                }
                if (const std::string cp = charPropText(f, c); !cp.empty()) { y += cp; break; }
                /// @note A binding set holding only folded bindings (and no binding to enable) is what ParseHky rebuilds
                ///       from the members alone: it is left out.
                if (f.name == "variableBindingSet" && &so == c.bindable && c.fold && !c.fold->bindings.empty())
                    if (const auto* set = dynamic_cast<const io::SchemaObject*>(v.obj.get());
                        set && set->GetInt("indexOfBindingToEnable") == -1 &&
                        std::all_of(set->Children("bindings").begin(), set->Children("bindings").end(),
                                    [&](const auto& b) { return c.fold->bindings.count(b.get()) != 0; }))
                        break;
                y += elemText(v.obj, childCtx(c, so, f), f.name, f.ref);
                break;
            case FieldKind::Struct:      y += elemText(v.obj, childCtx(c, so, f), f.name, f.ref); break;
            case FieldKind::PtrArray:
            case FieldKind::StructArray: {
                /// @note A bindable's folded bindings live in their members (charPropText), not in its bindings roster.
                std::vector<std::shared_ptr<IHavokObject>> kept;
                const bool filter = c.setFold && !c.setFold->bindings.empty() && f.name == "bindings";
                if (filter)
                    for (const auto& o : v.objs) if (!c.setFold->bindings.count(o.get())) kept.push_back(o);
                const auto& objs = filter ? kept : v.objs;
                if (!f.roster.empty() && !objs.empty())
                    if (auto r = rosterEntries(so, i, c, &objs)) {
                        if (c.sink) c.sink->push_back({ c.path + f.name, *r });
                        else y += f.name + ":\n" + indentLines(*r, "  ", "  ");
                        break;
                    }
                y += listText(objs, c, f.name, f.ref, &so, &f);
                break;
            }
            case FieldKind::StringArray:
                if (v.strs.empty()) break;
                y += f.name + ": " + flowList(v.strs.size(), [&](std::size_t k) { return text::Quote(v.strs[k]); }) + "\n";
                break;
            case FieldKind::ScalarArray: {
                const std::size_t w = static_cast<std::size_t>(schema::ScalarWidth(f.scalar));
                const std::size_t n = w ? v.raw.size() / w : 0;
                if (n == 0) break;
                if (f.boneWeights && f.scalar == Scalar::Float) { y += boneWeightsText(f, v.raw, c); break; }
                if (!f.boneOf.empty()) {
                    y += f.name + ": " + flowList(n, [&](std::size_t k) { return boneOfText(decodeInt(slice(v.raw, k, w), f.scalar), f, c); }) + "\n";
                    break;
                }
                if (f.boneList) {
                    y += f.name + ": " + flowList(n, [&](std::size_t k) { return boneText(decodeInt(slice(v.raw, k, w), f.scalar), c); }) + "\n";
                    break;
                }
                y += f.name + ": " + flowList(n, [&](std::size_t k) { return scalarText(f, slice(v.raw, k, w), c.reg); }) + "\n";
                break;
            }
            case FieldKind::Vec4Array:
                if (v.raw.empty()) break;
                y += f.name + ": " + flowList(v.raw.size() / 16, [&](std::size_t k) { return rvec(slice(v.raw, k, 16)); }) + "\n";
                break;
            case FieldKind::QsTransformArray:
                if (v.raw.empty()) break;
                y += f.name + ": " + flowList(v.raw.size() / 48, [&](std::size_t k) {
                    return flowList(3, [&](std::size_t j) { return rvec(slice(v.raw, k * 3 + j, 16)); });
                }) + "\n";
                break;
            case FieldKind::BoolArray:
                if (v.raw.empty()) break;
                y += f.name + ": " + flowList(v.raw.size(), [&](std::size_t k) { return boolstr(v.raw[k] != 0); }) + "\n";
                break;
            default: break;   // Vtable / Pad / Skip / EmptyArray / EmptyPtr carry no authored value
        }
    return y;
}

std::string reflectObject(const io::SchemaObject& so, const ReflectCtx& cin, const std::string& declaredClass) {
    ReflectCtx bc = cin;
    if (so.IndexOf("variableBindingSet")) {
        bc.bindable = &so;
        bc.member.clear();
        bc.fold     = foldFor(so, cin);
        bc.setFold  = nullptr;
    }
    const ReflectCtx& c = bc;
    std::string y = (declaredClass == so.ClassName()) ? std::string() : "class: " + std::string(so.ClassName()) + "\n";
    if (!so.Schema()->vocab.empty() && vocabRenderable(so) && !isFileNode(so.Child("stringData"), c)) {
        if (so.Schema()->vocab == "graph") return y + renderVocab(so, c);
        const auto fields = so.Fields();
        for (std::size_t i = 0; i < fields.size(); ++i) {
            if (vocabField(so, fields[i]->name)) continue;
            if (fields[i]->name != "stringData") { y += renderField(so, i, c); continue; }
            const auto* sd = so.Child("stringData");
            std::string body;
            const auto sf = sd->Fields();
            for (std::size_t j = 0; j < sf.size(); ++j)
                if (sf[j]->name != kCharacterProperties.names) body += renderField(*sd, j, subCtx(c, "stringData"));
            y += body.empty() ? std::string("stringData: {}\n") : "stringData:\n" + indentLines(body, "  ", "  ");
        }
        return y + renderVocab(so, c);
    }
    const auto fields = so.Fields();
    if (!c.skel) {
        for (std::size_t i = 0; i < fields.size(); ++i) y += renderField(so, i, c);
        return y;
    }
    ReflectCtx cc = c;
    for (std::size_t i = 0; i < fields.size(); ++i)
        if (fields[i]->kind == FieldKind::Ptr && fields[i]->ref == "hkaSkeleton")
            if (const auto it = c.skel->bones.find(so.Values()[i].obj.get()); it != c.skel->bones.end()) cc.boneOf[fields[i]->name] = &it->second;
    for (std::size_t i = 0; i < fields.size(); ++i) {
        if (skeletonViewTakes(so, *fields[i], c)) continue;
        y += renderField(so, i, cc);
    }
    return y;
}

} // namespace

/// @brief File-node categories plus `shared` for every inline object referenced from 2+ places.
/// @note  An inline object referenced from 2+ places is SHARED in the binary; nesting a copy under each parent would
///        parse back as N objects (the serializer then writes N). So it becomes its own node under `shared/<id>.yaml`,
///        referenced by id like any file node — the topology round-trips exactly.
std::unordered_map<const IHavokObject*, std::string> EffectiveCategories(const Identity& identity) {
    std::unordered_map<const IHavokObject*, std::string> effCat(identity.category.begin(), identity.category.end());
    {
        /// @note Count POINTER references across the whole graph, descending through embedded structs (a
        ///       transition-info element holds the `condition` pointer — it is not a top-level object itself).
        ///       Only pointers (Ptr/PtrArray) can share; embedded structs are owned by value.
        std::unordered_map<const IHavokObject*, int> refs;
        std::function<void(const io::SchemaObject&)> visit = [&](const io::SchemaObject& so) {
            const auto fields = so.Fields();
            const auto& vals  = so.Values();
            for (std::size_t i = 0; i < fields.size() && i < vals.size(); ++i) {
                const FieldKind k = fields[i]->kind;
                if (k == FieldKind::Ptr || k == FieldKind::PtrArray) {
                    if (vals[i].obj) ++refs[vals[i].obj.get()];
                    for (const auto& e : vals[i].objs) if (e) ++refs[e.get()];
                } else if (k == FieldKind::Struct || k == FieldKind::StructArray) {
                    if (const auto* e = dynamic_cast<const io::SchemaObject*>(vals[i].obj.get())) visit(*e);
                    for (const auto& o : vals[i].objs) if (const auto* e = dynamic_cast<const io::SchemaObject*>(o.get())) visit(*e);
                }
            }
        };
        /// @note A PARSED identity also lists the embedded struct objects themselves; visiting one on its own as well as
        ///       through its owner would count its pointers twice (a once-referenced condition would read as shared).
        std::unordered_set<const IHavokObject*> embedded;
        for (const auto& [obj, cat] : identity.category)
            if (const auto* so = dynamic_cast<const io::SchemaObject*>(obj)) {
                const auto fields = so->Fields();
                const auto& vals  = so->Values();
                for (std::size_t i = 0; i < fields.size() && i < vals.size(); ++i)
                    if (fields[i]->kind == FieldKind::Struct || fields[i]->kind == FieldKind::StructArray) {
                        if (vals[i].obj) embedded.insert(vals[i].obj.get());
                        for (const auto& o : vals[i].objs) if (o) embedded.insert(o.get());
                    }
            }
        for (const auto& [obj, cat] : identity.category)
            if (const auto* so = dynamic_cast<const io::SchemaObject*>(obj); so && !embedded.count(obj)) visit(*so);
        for (auto& [obj, cat] : effCat)
            if (cat.empty() && refs[obj] >= 2 && std::string(obj->ClassName()) != "hkRootLevelContainer") cat = "shared";
    }
    return effCat;
}

namespace {

/// @brief A skeleton's bone names, in order.
std::vector<std::string> boneNamesOf(const io::SchemaObject& skel) {
    std::vector<std::string> out;
    for (const auto& b : skel.Children("bones"))
        if (const auto* bo = dynamic_cast<const io::SchemaObject*>(b.get())) out.push_back(bo->Str("name"));
    return out;
}

/// @brief Whether a skeleton's three bone arrays line up and its bones can be keyed by name: one parent, one pose and a
///        unique, non-empty name per bone, every parent a bone of the skeleton or -1.
/// @note Unique by path identity too: a bone's name is its file's name, and a packed .hky keys files case-folded, so two
///       bones differing only in case would share one file there.
bool keyableSkeleton(const io::SchemaObject& skel, const std::vector<std::string>& names) {
    const auto parents = skel.GetArray<std::int16_t>("parentIndices");
    if (parents.size() != names.size() || skel.Values()[*skel.IndexOf("referencePose")].raw.size() != names.size() * 48) return false;
    std::set<std::string> seen;
    for (std::size_t i = 0; i < names.size(); ++i)
        if (names[i].empty() || !seen.insert(codec::resource::PathKey(names[i])).second || parents[i] < -1 ||
            parents[i] >= static_cast<int>(names.size()))
            return false;
    return true;
}

/// @brief The skeleton view over a unit's graph, or none when it holds no hkaSkeleton. A skeleton takes a role only
///        when the unit's shape is the vanilla one (the container's skeletons[0], and skeletons[1] = the ragdoll
///        instance's) and its bones are keyable; anything else stays in the generic form, still lossless.
std::optional<SkeletonView> buildSkeletonView(const Identity& identity) {
    SkeletonView v;
    const io::SchemaObject* container = nullptr;
    const io::SchemaObject* instance  = nullptr;
    for (const auto& [obj, cat] : identity.category) {
        const auto* so = dynamic_cast<const io::SchemaObject*>(obj);
        if (!so) continue;
        const std::string cls = so->ClassName();
        if (cls == "hkaSkeleton") v.bones[so] = boneNamesOf(*so);
        else if (cls == "hkaAnimationContainer" && !container) container = so;
        else if (cls == "hkaRagdollInstance" && !instance) instance = so;
    }
    if (v.bones.empty()) return std::nullopt;
    const auto take = [&](const IHavokObject* o, const char* role) {
        const auto* so = dynamic_cast<const io::SchemaObject*>(o);
        if (so && keyableSkeleton(*so, v.bones.at(so))) v.role[so] = role;
    };
    const auto& skels = container ? container->Children("skeletons") : std::vector<std::shared_ptr<IHavokObject>>{};
    if (skels.empty() || skels.size() > 2 || !skels[0]) return v;
    const IHavokObject* ragdoll = instance ? instance->Child("skeleton") : nullptr;
    if (skels.size() == 2 && skels[1].get() != ragdoll) return v;
    take(skels[0].get(), "animation");
    if (ragdoll && ragdoll != skels[0].get()) take(ragdoll, "ragdoll");
    /// @note The map folds onto the ragdoll bones only when it has one entry per bone, each a body of the instance or -1.
    if (ragdoll && v.role.count(ragdoll)) {
        const auto map = instance->GetArray<std::int32_t>("boneToRigidBodyMap");
        const auto& bodies = instance->Children("rigidBodies");
        bool fits = map.size() == v.bones.at(ragdoll).size();
        for (const std::int32_t m : map) fits = fits && m >= -1 && m < static_cast<std::int32_t>(bodies.size()) && (m < 0 || bodies[static_cast<std::size_t>(m)]);
        if (fits) v.ragdollInstance = instance;
    }
    return v;
}

/// @brief One bone file of a role skeleton: its parent by name (`null` for a root), lockTranslation, its reference pose,
///        and for a folded ragdoll bone the body it drives.
std::string boneFile(const io::SchemaObject& skel, std::size_t i, const std::vector<std::string>& names, const std::string* body) {
    const auto parents = skel.GetArray<std::int16_t>("parentIndices");
    const auto& pose   = skel.Values()[*skel.IndexOf("referencePose")].raw;
    const auto* bone   = dynamic_cast<const io::SchemaObject*>(skel.Children("bones")[i].get());
    const auto  p      = slice(pose, i, 48);
    std::string y = "parent: " + (parents[i] < 0 ? std::string("null") : text::Quote(names[static_cast<std::size_t>(parents[i])])) + "\n";
    y += "lockTranslation: " + boolstr(bone->GetBool("lockTranslation")) + "\n";
    y += "pose:\n  translation: " + rvec(slice(p, 0, 16)) + "\n  rotation: " + rvec(slice(p, 1, 16)) + "\n  scale: " + rvec(slice(p, 2, 16)) + "\n";
    if (body) y += "rigidBody: " + (body->empty() ? std::string("null") : *body) + "\n";
    return y;
}

}  // namespace

bool EmitHkyReflective(const Identity& source, const schema::SchemaRegistry& reg,
                       const std::string& outDir, std::string& err, const std::vector<std::string>* bones,
                       const std::string& unitPath) {
    namespace fs = std::filesystem;
    const std::optional<SkeletonView> skel = buildSkeletonView(source);
    /// @note A role skeleton's id IS its role: graph.yaml and the mappers name `animation` / `ragdoll`.
    Identity roleIds;
    if (skel && !skel->role.empty()) {
        roleIds = source;
        for (const auto& [obj, role] : skel->role) roleIds.ids[obj] = role;
    }
    const Identity& identity = roleIds.ids.empty() ? source : roleIds;
    const NameResolver nr = buildResolver(identity);
    auto effCat = EffectiveCategories(identity);
    if (skel) for (const auto& [obj, role] : skel->role) effCat[obj] = role;
    const skeleton::BoneOrderEntry* order = skel && !skel->role.empty() ? skeleton::FindBoneOrder(reg, unitPath) : nullptr;
    for (const auto& [obj, cat] : effCat) {
        const auto* so = dynamic_cast<const io::SchemaObject*>(obj);
        if (!so) { err = "EmitHkyReflective expects a SchemaObject graph"; return false; }
        const bool root = std::string(so->ClassName()) == "hkRootLevelContainer";
        if (cat.empty() && !root) continue;
        const std::string idStr = identity.ids.at(obj);
        std::vector<std::pair<std::string, std::string>> rosters;
        ReflectCtx ctx{ identity, nr, reg, bones, &effCat };
        if (skel) ctx.skel = &*skel;
        if (skel && skel->role.count(obj)) {
            /// @note A role skeleton is the folder `<role>/`: skeleton.yaml (its other members, plus `extraBones:` in
            ///       their order when the shim lists only some of its bones) and bones/<bone>.yaml per bone. Its order
            ///       must be the shim's binding, or the unit could not reproduce it.
            const std::string& role  = skel->role.at(obj);
            const auto&        names = skel->bones.at(obj);
            const std::vector<std::string>* known = nullptr;
            if (order) if (const auto it = order->roles.find(role); it != order->roles.end()) known = &it->second;
            if (skeleton::BindBoneOrder(known, names) != names) {
                err = unitPath + ": the " + role + " skeleton's bone order disagrees with the boneorder shim";
                return false;
            }
            std::vector<std::string> extra;
            const std::set<std::string> listed = known ? std::set<std::string>(known->begin(), known->end()) : std::set<std::string>{};
            for (const auto& b : names) if (!listed.count(b)) extra.push_back(b);
            std::string body = "id: " + role + "\n" + reflectObject(*so, ctx);
            if (!extra.empty()) body += "extraBones: " + flowList(extra.size(), [&](std::size_t k) { return text::Quote(extra[k]); }) + "\n";
            const fs::path dir = fs::path(outDir) / role;
            if (!disk::WriteFile(dir / "skeleton.yaml", body, {}, &err)) return false;
            const auto map = skel->ragdollInstance && role == "ragdoll" ? skel->ragdollInstance->GetArray<std::int32_t>("boneToRigidBodyMap") : std::vector<std::int32_t>{};
            for (std::size_t i = 0; i < names.size(); ++i) {
                std::string rb;
                if (!map.empty() && map[i] >= 0) rb = refIdOf(skel->ragdollInstance->Children("rigidBodies")[static_cast<std::size_t>(map[i])], identity);
                if (!disk::WriteFile(dir / "bones" / (names[i] + ".yaml"), boneFile(*so, i, names, map.empty() ? nullptr : &rb), {}, &err)) return false;
            }
            continue;
        }
        if (!root) ctx.sink = &rosters;
        const std::string body = reflectObject(*so, ctx);
        /// @note A node with sub-records is a FOLDER: its body `<cat>/<id>/<id>.yaml` beside its rosters
        ///       `<cat>/<id>/<field.path>.yaml`; a node without any is the single file `<cat>/<id>.yaml`.
        const fs::path dir = root ? fs::path(outDir) : rosters.empty() ? fs::path(outDir) / cat : fs::path(outDir) / cat / fsId(idStr);
        if (!disk::WriteFile(dir / (root ? std::string("graph.yaml") : fsId(idStr) + ".yaml"), "id: " + idStr + "\n" + body, {}, &err))
            return false;
        for (const auto& [path, entries] : rosters)
            if (!disk::WriteFile(dir / (path + ".yaml"), entries, {}, &err)) return false;
    }
    return true;
}

namespace { std::string formIdOf(std::uint16_t idx, const std::string& decId); }

bool DecompileBehaviorReflective(const std::vector<std::uint8_t>& bytes, const schema::SchemaRegistry& reg,
                                 const std::string& outDir, std::string& err, const std::vector<std::string>* bones,
                                 const std::string& unitPath) {
    try {
        PackFileDeserializer des;
        des.ObjectFactory = io::MakeSchemaFactory(reg);
        BinaryReaderEx br(false, true, bytes);
        des.Deserialize(br);
        Identity id = AssignIdentity(des, reg);
        for (auto& [o, v] : id.ids) v = formIdOf(CB::engine::codec::formid::SELF_INDEX, v);
        return EmitHkyReflective(id, reg, outDir, err, bones, unitPath);
    } catch (const std::exception& e) {
        err = std::string("DecompileBehaviorReflective: ") + e.what();
        return false;
    }
}

// ── generic tagfile-XML PARSE (the inverse codec) ────────────────────────────────────────────────
namespace {
namespace en = CB::engine::common::enums;
using schema::Field;
using schema::FieldKind;
using schema::Scalar;

// Nemesis symbol table: variable/event NAME -> roster INDEX. A patch writes a binding's variableIndex (or
// a transition's eventId, …) as `$variableID[Name]$` / `$eventID[Name]$` — a placeholder resolved to the
// merged roster index at convert time. Built from the merged graph's hkbBehaviorGraphStringData before the
// field walk; without it those fields parse to 0 and every mod binding/event points at the wrong slot.
struct SymTab {
    std::unordered_map<std::string, int> var, evt;
};
// If `text` is a Nemesis symbol, resolve it to its roster index (unresolved -> -1); else return LONG_MIN.
long resolveNemesisSymbol(const std::string& text, const SymTab* syms) {
    if (!syms) return std::numeric_limits<long>::min();
    auto lookup = [&](std::size_t pfx, const std::unordered_map<std::string, int>& m) -> long {
        const auto rb = text.find(']', pfx);
        const std::string nm = text.substr(pfx, rb == std::string::npos ? std::string::npos : rb - pfx);
        auto it = m.find(nm); return it != m.end() ? it->second : -1;
    };
    if (text.rfind("$variableID[", 0) == 0) return lookup(12, syms->var);
    if (text.rfind("$eventID[", 0) == 0)    return lookup(9, syms->evt);
    return std::numeric_limits<long>::min();
}

// Encoders — the exact inverses of decodeInt/decodeFloat/tfVec4 (little-endian raw slots).
std::vector<std::uint8_t> encInt(std::int64_t v, Scalar s) {   // int64 (not long): see decodeInt / #6
    const int w = schema::ScalarWidth(s);
    std::vector<std::uint8_t> r(w, 0);
    const std::uint64_t u = static_cast<std::uint64_t>(v);
    for (int i = 0; i < w; ++i) r[i] = static_cast<std::uint8_t>((u >> (8 * i)) & 0xff);
    return r;
}
std::vector<std::uint8_t> encFloat(float f) {
    std::vector<std::uint8_t> r(4); std::memcpy(r.data(), &f, 4); return r;
}
float parseF(const std::string& t) { return std::strtof(t.c_str(), nullptr); }
std::int64_t parseL(const std::string& t) { return std::strtoll(t.c_str(), nullptr, 0); }   // 0 base: dec/0xHEX; int64 (#6)

std::vector<std::uint8_t> parseVec4Raw(const std::string& t) {
    const auto q = CB::engine::common::vec4::parseVec4(t);
    std::vector<std::uint8_t> r(16); std::memcpy(r.data(), q.data(), 16); return r;
}

// Flags text -> value. Strips the tagfile's "<!-- UNKNOWN BITS -->" marker (tfFlags form) so the leftover
// "0xNNNN" parses as a bare integer; ResolveEnum then absorbs both the tfFlags form and FormatFlags form.
long parseFlagsText(const std::string& text, const std::unordered_map<std::string, long>& table) {
    std::string t = text;
    for (const std::string mark = "<!-- UNKNOWN BITS -->";;) {
        const auto p = t.find(mark); if (p == std::string::npos) break;
        t.erase(p, mark.size());
    }
    return en::ResolveEnum(t, table);
}

/// @brief The enum table a scalar field's tagfile text parses through: the schema `enum:`, else the overrides for
///        fields the schema keeps as plain ints (hkbVariableInfo::type, hkbRoleAttribute::role/flags).
const std::unordered_map<std::string, long>* scalarEnumTable(const std::string& cls, const Field& f, bool& isFlags) {
    isFlags = f.isFlags;
    if (!f.enumName.empty()) return enumTable(f.enumName);
    if (cls == "hkbVariableInfo"  && f.name == "type") return &en::VariableType();
    if (cls == "hkbRoleAttribute" && f.name == "role") return &en::Role();
    if (cls == "hkbRoleAttribute" && f.name == "flags") { isFlags = true; return &en::RoleFlags(); }
    return nullptr;
}

// One scalar field's tagfile text -> raw bytes (inverse of renderScalar + scalarEnumTable's enum overrides).
std::vector<std::uint8_t> parseScalarRaw(const std::string& cls, const Field& f, const std::string& text,
                                         const SymTab* syms, std::string& err) {
    if (f.scalar == Scalar::Float) return encFloat(parseF(text));
    if (f.scalar == Scalar::Bool)  return encInt(text == "true" ? 1 : 0, f.scalar);
    // A Nemesis $variableID[Name]$ / $eventID[Name]$ placeholder -> its merged-roster index.
    if (const long sv = resolveNemesisSymbol(text, syms); sv != std::numeric_limits<long>::min()) {
        if (sv < 0) {   // BR review #8: a $…ID[Name]$ whose NAME isn't in the merged roster resolves to
            err = "unresolved Nemesis symbol '" + text + "' (name not in the merged roster)";
            return {};  // -1; baking it silently points every such binding/event at the wrong slot. Fail loud.
        }
        return encInt(sv, f.scalar);
    }
    bool isFlags = false;
    const auto* t = scalarEnumTable(cls, f, isFlags);
    long v;
    if (t) v = isFlags ? parseFlagsText(text, *t) : en::ResolveEnum(text, *t);
    else   v = parseL(text);
    return encInt(v, f.scalar);
}

// The <hkparam name="F"> child of an object node (nullptr if absent — a field the patch didn't set).
const CB::engine::common::xml::Node* paramByName(const CB::engine::common::xml::Node& obj, const std::string& name) {
    for (const auto& c : obj.children)
        if (c.tag == "hkparam" && c.attr("name") == name) return &c;
    return nullptr;
}

// Split whitespace-delimited tokens (ScalarArray elements, PtrArray refs).
std::vector<std::string> splitWs(const std::string& s) {
    std::vector<std::string> out; std::string cur;
    for (char c : s) {
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') { if (!cur.empty()) { out.push_back(cur); cur.clear(); } }
        else cur += c;
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

// Split a Vec4Array hkparam's text into per-element float-quads, in order. Accepts BOTH the
// decompiler's "(x y z w)(...)..." groups AND a Nemesis/vanilla bare, whitespace/newline-separated
// float run (4 floats per element) — same parens-vs-bare split as parseVec4Raw (BR-28). parseVec4Raw
// then parses each returned quad (it tolerates both forms).
std::vector<std::string> splitVec4s(const std::string& s) {
    std::vector<std::string> out;
    if (s.find('(') != std::string::npos) {                    // decompiler form: "(...)" groups
        for (std::size_t i = 0; i < s.size();) {
            const auto a = s.find('(', i); if (a == std::string::npos) break;
            const auto b = s.find(')', a); if (b == std::string::npos) break;
            out.push_back(s.substr(a, b - a + 1)); i = b + 1;
        }
        return out;
    }
    // bare form: whitespace-separated floats, 4 per vec4.
    std::vector<std::string> toks;
    for (std::size_t i = 0; i < s.size();) {
        while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
        std::size_t j = i;
        while (j < s.size() && !std::isspace(static_cast<unsigned char>(s[j]))) ++j;
        if (j > i) toks.push_back(s.substr(i, j - i));
        i = j;
    }
    for (std::size_t k = 0; k + 3 < toks.size(); k += 4)
        out.push_back(toks[k] + " " + toks[k + 1] + " " + toks[k + 2] + " " + toks[k + 3]);
    return out;
}

// Build a fresh SchemaObject for `cls` (Init'd, empty), or nullptr if the registry doesn't know it.
std::shared_ptr<io::SchemaObject> makeObj(const schema::SchemaRegistry& reg, const std::string& cls) {
    const schema::ClassSchema* cs = reg.Find(cls);
    if (!cs) return nullptr;
    auto so = std::make_shared<io::SchemaObject>(&reg, cs);
    so->Init();
    return so;
}

/// @brief A Havok-XML string value as CB's model holds it: Havok's null-string marker (U+2400, written `&#9216;`)
///        reads as the empty string, which is how the reflective master holds a null string.
/// @note Why: kept as the marker, an unset string (a sync clip's role prefix) diffed against the master's '' and
///       the converter emitted a patch setting it to the marker; BSSynchronizedClipGenerator binds and samples its
///       role by that prefix, which then matches no track.
std::string havokXmlString(const std::string& text) {
    static constexpr std::string_view kNullMarker = "\xE2\x90\x80";
    return text == kNullMarker ? std::string() : text;
}

// Fill an already-created SchemaObject's fields from its tagfile <hkobject> node. `idMap` resolves refs
// by CANONICAL id (populated in pass 1, so every top-level id — base #NNNN or new-node #code$N — exists).
// Inline structs recurse.
void fillObject(io::SchemaObject& so, const CB::engine::common::xml::Node& node,
                const schema::SchemaRegistry& reg,
                const std::unordered_map<std::string, std::shared_ptr<io::SchemaObject>>& idMap,
                const SymTab* syms, std::string& err) {
    const std::string cls = so.ClassName();
    const auto fields = so.Fields();
    for (std::size_t i = 0; i < fields.size(); ++i) {
        const Field& f = *fields[i];
        if (f.name.empty() || f.ignored) continue;         // vtable/skip/pad + SERIALIZE_IGNORED: no hkparam
        const CB::engine::common::xml::Node* p = paramByName(node, f.name);
        if (!p) continue;                                  // field absent -> keep prior value (Init default,
                                                           //   or an earlier merge layer's value)
        io::FieldValue& val = so.FieldAt(i);
        val = io::FieldValue{};   // reset before (re)fill: a present field REPLACES — arrays must not
                                  // accumulate across merge layers, and a patch array is the whole array
        switch (f.kind) {
            case FieldKind::Scalar:  val.raw = parseScalarRaw(cls, f, p->text, syms, err); if (!err.empty()) return; break;
            case FieldKind::String:
            case FieldKind::CString: val.str = havokXmlString(p->text); break;
            case FieldKind::Vector4:
            case FieldKind::Quaternion:
                if (f.count <= 1) { val.raw = parseVec4Raw(p->text); break; }
                for (const std::string& g : splitVec4s(p->text)) {
                    const auto b = parseVec4Raw(g); val.raw.insert(val.raw.end(), b.begin(), b.end());
                }
                break;
            case FieldKind::Ptr: {
                const auto ref = [&](const std::string& r) -> std::shared_ptr<IHavokObject> {
                    if (r.size() < 2 || r[0] != '#') return nullptr;
                    auto it = idMap.find(canonId(r));
                    return it != idMap.end() ? it->second : nullptr;
                };
                if (f.count <= 1) { val.obj = ref(p->text); break; }
                for (const std::string& r : splitWs(p->text)) val.objs.push_back(ref(r));
                break;
            }
            case FieldKind::PtrArray: {
                for (const std::string& r : splitWs(p->text)) {
                    if (r.size() > 1 && r[0] == '#') {
                        auto it = idMap.find(canonId(r));
                        val.objs.push_back(it != idMap.end() ? it->second : nullptr);
                    } else if (r == "null") {
                        // A null element: emit writes the literal "null" token (refTag), so parse MUST
                        // push a null slot to preserve numelements + every later index. Dropping it
                        // shrinks the array and shifts index-addressed refs (stateId, selector index).
                        val.objs.push_back(nullptr);
                    }
                }
                break;
            }
            case FieldKind::StringArray:
                for (const auto& c : p->children) if (c.tag == "hkcstring") val.strs.push_back(havokXmlString(c.text));
                break;
            case FieldKind::Vec4Array: {
                for (const std::string& g : splitVec4s(p->text)) {
                    const auto b = parseVec4Raw(g); val.raw.insert(val.raw.end(), b.begin(), b.end());
                }
                break;
            }
            case FieldKind::ScalarArray: {
                for (const std::string& tok : splitWs(p->text)) {
                    const auto b = (f.scalar == Scalar::Float) ? encFloat(parseF(tok)) : encInt(parseL(tok), f.scalar);
                    val.raw.insert(val.raw.end(), b.begin(), b.end());
                }
                break;
            }
            case FieldKind::Struct: {
                if (const CB::engine::common::xml::Node* inner = p->child("hkobject")) {
                    auto sub = makeObj(reg, f.ref);
                    if (!sub) { err = cls + "." + f.name + ": unknown inline struct class '" + f.ref + "'"; return; }
                    fillObject(*sub, *inner, reg, idMap, syms, err); if (!err.empty()) return;
                    val.obj = sub;
                }
                break;
            }
            case FieldKind::StructArray: {
                for (const auto& c : p->children) {
                    if (c.tag != "hkobject") continue;
                    auto sub = makeObj(reg, f.ref);
                    if (!sub) { err = cls + "." + f.name + ": unknown struct-array class '" + f.ref + "'"; return; }
                    fillObject(*sub, c, reg, idMap, syms, err); if (!err.empty()) return;
                    val.objs.push_back(sub);
                }
                break;
            }
            default: break;   // QsTransform(Array)/BoolArray/EmptyArray/EmptyPtr — not emitted, not parsed
        }
    }
}

// Recursively collect every top-level object node (tag "hkobject" with a "#NNNN" name attr). Inline
// struct hkobjects carry no name and are reached through their owner's field, never here.
void collectTopLevel(const CB::engine::common::xml::Node& n, std::vector<const CB::engine::common::xml::Node*>& out) {
    if (n.tag == "hkobject") {
        const auto nm = n.attr("name");
        if (!nm.empty() && nm.front() == '#') { out.push_back(&n); return; }   // don't descend into a named obj
    }
    for (const auto& c : n.children) collectTopLevel(c, out);
}

/// @brief Expand a patch layer's SNIPPET element arrays to the full array the mod intends. A Nemesis array param
///        whose `numelements` is not a count (e.g. `"existing"`) lists only a window: the lines outside MOD_CODE are
///        base context, not the whole array — taken verbatim, a lone changer's snippet REPLACED the array (CB-6:
///        0_master's 1217 events -> 7). The mod's view of its base (`orig`, via StripToOriginal) is a contiguous run
///        of the base array; the layer's (modded) window is spliced over that run.
/// @note Ambiguous runs (eventInfos context is `{flags: 0}` objects, which match almost anywhere) anchor at the
///       LAST match: a snippet extends the roster tail. A window that matches nowhere is an error, never a truncation.
/// @note CONTEXT IS NOT CONTENT, for a whole array too: a mod that writes out the full array (its view of the base has
///       the base's length) copies the base's lines outside MOD_CODE from its tool's template — which can differ
///       (Nemesis templates trim whitespace: vanilla's ' iState_NPCSneaking' arrives as a duplicate
///       'iState_NPCSneaking'). The lines its original and modded views share are its untouched ones; each takes the
///       base's line at the same position, verbatim (Havok names are whitespace- and case-sensitive).
/// @return true with `out` = the expanded layer when any param was anchored; false (out untouched) when none was.
bool anchorSnippets(const CB::engine::common::xml::Node& base, const CB::engine::common::xml::Node& layer,
                    const CB::engine::common::xml::Node* orig, CB::engine::common::xml::Node& out, std::string& err) {
    namespace xml = CB::engine::common::xml;
    auto param = [](const xml::Node& n, std::string_view name) -> const xml::Node* {
        for (const auto& c : n.children) if (c.tag == "hkparam" && c.attr("name") == name) return &c;
        return nullptr;
    };
    auto isElementArray = [](const xml::Node& p) {
        for (const auto& c : p.children) if (c.tag == "hkobject" || c.tag == "hkcstring") return true;
        return false;
    };
    auto isSnippet = [&](const xml::Node& p) {
        const std::string_view ne = p.attr("numelements");
        return !ne.empty() && !std::all_of(ne.begin(), ne.end(), [](char ch) { return std::isdigit(static_cast<unsigned char>(ch)) != 0; }) &&
               isElementArray(p);
    };
    bool any = false;
    for (std::size_t i = 0; i < layer.children.size(); ++i) {
        const xml::Node& lp = layer.children[i];
        if (lp.tag != "hkparam" || !isElementArray(lp)) continue;
        const std::string name(lp.attr("name"));
        const xml::Node* bp = param(base, name);
        const xml::Node* op = orig ? param(*orig, name) : nullptr;
        if (!bp || !op) continue;
        const auto& B = bp->children;
        const auto& O = op->children;
        if (!isSnippet(lp)) {
            const auto& L = lp.children;
            bool same = O.size() == B.size();
            if (!same) continue;
            for (std::size_t k = 0; same && k < O.size(); ++k) same = CB::engine::merge::deepEqual(O[k], B[k]);
            if (same) continue;
            const std::size_t n = O.size(), m = L.size();
            std::vector<std::uint32_t> dp((n + 1) * (m + 1), 0);
            for (std::size_t a = n; a-- > 0;)
                for (std::size_t b = m; b-- > 0;)
                    dp[a * (m + 1) + b] = CB::engine::merge::deepEqual(O[a], L[b])
                        ? dp[(a + 1) * (m + 1) + b + 1] + 1
                        : std::max(dp[(a + 1) * (m + 1) + b], dp[a * (m + 1) + b + 1]);
            std::vector<xml::Node> fixed = L;
            bool changed = false;
            for (std::size_t a = 0, b = 0; a < n && b < m;) {
                if (CB::engine::merge::deepEqual(O[a], L[b]) && dp[a * (m + 1) + b] == dp[(a + 1) * (m + 1) + b + 1] + 1) {
                    if (!CB::engine::merge::deepEqual(O[a], B[a])) { fixed[b] = B[a]; changed = true; }
                    ++a; ++b;
                } else if (dp[(a + 1) * (m + 1) + b] >= dp[a * (m + 1) + b + 1]) ++a;
                else ++b;
            }
            if (!changed) continue;
            if (!any) { out = layer; any = true; }
            out.children[i].children = std::move(fixed);
            continue;
        }
        std::size_t at = std::string::npos;
        for (std::size_t k = 0; O.size() <= B.size() && k + O.size() <= B.size(); ++k) {
            bool eq = true;
            for (std::size_t j = 0; eq && j < O.size(); ++j) eq = CB::engine::merge::deepEqual(B[k + j], O[j]);
            if (eq) at = k;
        }
        if (at == std::string::npos) {
            err = "#" + std::string(layer.attr("name")) + " " + name + ": the patch's numelements=\"" +
                  std::string(lp.attr("numelements")) + "\" window matches no run of the base array";
            return false;
        }
        if (!any) { out = layer; any = true; }
        xml::Node& np = out.children[i];
        np.children.assign(B.begin(), B.begin() + static_cast<std::ptrdiff_t>(at));
        np.children.insert(np.children.end(), lp.children.begin(), lp.children.end());
        np.children.insert(np.children.end(), B.begin() + static_cast<std::ptrdiff_t>(at + O.size()), B.end());
        for (auto& [k, v] : np.attrs) if (k == "numelements") v = std::to_string(np.children.size());
    }
    return any;
}

// The shared parse core. `sources` is a precedence-ordered list of tagfile texts: per #NNNN, the LAST
// source that defines the id WINS (so patches override the base), and refs resolve across the MERGED id
// space (a patch object may point at a base-only node). Single-source parse and base+patch merge are the
// same operation with one vs many sources. `deltaIds` (optional) receives the ids a `isDelta` source won.
bool parseSourcesMerged(const std::vector<std::pair<const std::string*, bool>>& sources,
                        const schema::SchemaRegistry& reg, ParsedTagfile& out,
                        std::vector<std::string>* deltaIds, std::string& err) {
    out = ParsedTagfile{};
    // Parse every source into a persistent tree (reserve so the vector never reallocates — the node
    // pointers below alias into these trees). Wrap each in a synthetic root: a source is a bare run of
    // sibling <hkobject> and xml::Parse returns only the first element.
    std::vector<CB::engine::common::xml::Node> roots; roots.reserve(sources.size());
    std::vector<CB::engine::common::xml::Node> origRoots; origRoots.reserve(sources.size());   ///< deltas as their mod saw the base

    // Per CANONICAL id, the ORDERED layers that define it (base first, then each overriding patch). Each
    // layer records whether it is a delta and, for a delta, the fields it ACTUALLY changed (its MOD_CODE
    // delta, via the shared changedFields). The merge is NO LONGER a field-wise last-writer overlay —
    // every id's layers go through the SHARED CB::engine::merge::bashMerge, so an array field edited by 2+
    // mods (variableNames/eventNames rosters, clip triggers, state/transition lists) is UNIONED.
    // Field-wise last-writer here silently dropped all-but-the-last
    // mod's array additions — the TDM_Pitch A-pose when one mod's several Nemesis codes are merged into
    // one bundle. New-node ids (#code$N) live in the same map as base #NNNN, so refs resolve either way.
    struct Layer { const CB::engine::common::xml::Node* node; bool isDelta; const std::unordered_set<std::string>* changed;
                   const CB::engine::common::xml::Node* orig = nullptr; };
    std::unordered_map<std::string, std::vector<Layer>> layers;
    std::unordered_map<std::string, bool> touchedByDelta;   // id was overridden/added by a patch source
    std::vector<std::string> order;                         // first-seen id order (base first, new ids appended)
    std::vector<std::unique_ptr<std::unordered_set<std::string>>> changedHold;  // owns the per-delta changed-sets

    for (const auto& [text, isDelta] : sources) {
        std::string parseText = *text;
        const std::unordered_set<std::string>* changedPtr = nullptr;
        if (isDelta) {
            // changedFields wants the RAW patch (with MOD_CODE markers) for an accurate delta; on
            // already-stripped input it degrades to all-fields, which bashMerge's changer filter still
            // narrows to real edits. StripPatchOriginals is idempotent (no-op on a clean node).
            changedHold.push_back(std::make_unique<std::unordered_set<std::string>>(CB::engine::merge::changedFields(*text)));
            changedPtr = changedHold.back().get();
            // Pandora-compat (CONVERTER-ONLY): reproduce Pandora/Nemesis's occurrence-counted
            // placement for numeric text-array params (boneWeights &c) BEFORE the positional
            // strip, so converted bundles match the ecosystem's output. Element params are left
            // for StripPatchOriginals below.
            CB::engine::compat::ApplyNemesisTextArrayEdits(parseText);
            CB::engine::common::xml::StripPatchOriginals(parseText);
        }
        roots.push_back(CB::engine::common::xml::Parse("<r>" + parseText + "</r>"));
        std::vector<const CB::engine::common::xml::Node*> nodes;
        collectTopLevel(roots.back(), nodes);
        std::unordered_map<std::string, const CB::engine::common::xml::Node*> origById;
        if (isDelta) {
            std::string origText = *text;
            CB::engine::common::xml::StripToOriginal(origText);
            origRoots.push_back(CB::engine::common::xml::Parse("<r>" + origText + "</r>"));
            std::vector<const CB::engine::common::xml::Node*> on;
            collectTopLevel(origRoots.back(), on);
            for (const auto* o : on) origById[canonId(std::string(o->attr("name")))] = o;
        }
        for (const CB::engine::common::xml::Node* n : nodes) {
            const std::string id = canonId(std::string(n->attr("name")));
            if (layers.find(id) == layers.end()) order.push_back(id);
            const auto oi = origById.find(id);
            layers[id].push_back({ n, isDelta, changedPtr, oi == origById.end() ? nullptr : oi->second });
            if (isDelta) touchedByDelta[id] = true;
        }
    }
    if (order.empty()) { err = "parseSourcesMerged: no #NNNN hkobjects found"; return false; }

    // Pass 1 — create one object per id (so refs resolve in pass 2). Its class is the LAST layer's (a
    // patch override keeps the base class; a new node's only layer is the patch).
    std::unordered_map<std::string, std::shared_ptr<io::SchemaObject>> idMap;
    std::vector<std::pair<std::string, std::shared_ptr<io::SchemaObject>>> work;
    out.objects.reserve(order.size());
    for (const std::string& id : order) {
        const std::string cls = std::string(layers[id].back().node->attr("class"));
        auto so = makeObj(reg, cls);
        if (!so) { err = "parseSourcesMerged: unknown class '" + cls + "' for #" + id; return false; }
        idMap[id] = so;
        out.objects.push_back(so);
        work.push_back({ id, so });
        out.identity.ids[so.get()]      = id;
        out.identity.category[so.get()] = CategoryForClass(cls);
        if (deltaIds && touchedByDelta[id]) deltaIds->push_back(id);
    }

    // Bashed-merge each id's layers into ONE node via the shared core. Base = the (single) non-delta
    // layer, or — for a new node with no vanilla base — the first layer as the seed. Delta layers whose
    // class differs from the object (source drift) are dropped from the merge, preserving the old skip.
    std::unordered_map<std::string, CB::engine::common::xml::Node> mergedById;
    mergedById.reserve(order.size());
    for (const std::string& id : order) {
        const std::vector<Layer>& ls = layers[id];
        const std::string objCls(idMap[id]->ClassName());
        const CB::engine::common::xml::Node* baseNode = nullptr;
        for (const Layer& L : ls) if (!L.isDelta) baseNode = L.node;   // the base node (normally exactly one)
        std::size_t seedSkip = 0;
        if (!baseNode) { baseNode = ls.front().node; seedSkip = 1; }   // new node: first layer seeds the merge

        std::vector<CB::engine::merge::PatchLayer> pls; pls.reserve(ls.size());
        std::size_t seen = 0;
        for (const Layer& L : ls) {
            const bool isSeed = (seedSkip == 1 && seen++ == 0);
            if (!L.isDelta || isSeed) continue;
            if (std::string(L.node->attr("class")) != objCls) continue;   // source drift — skip this layer
            CB::engine::common::xml::Node anchored;
            std::string aerr;
            const bool expanded = seedSkip == 0 && anchorSnippets(*baseNode, *L.node, L.orig, anchored, aerr);
            if (!aerr.empty()) { err = "parseSourcesMerged: " + aerr; return false; }
            pls.push_back({ expanded ? std::move(anchored) : *L.node, L.changed ? *L.changed : std::unordered_set<std::string>{} });
        }
        std::vector<const CB::engine::merge::PatchLayer*> ptrs; ptrs.reserve(pls.size());
        for (const auto& p : pls) ptrs.push_back(&p);
        /// @note Only this tagfile merge reads `compose`; the runtime's reflective merge (MergeLayersReflective -> patchField) reads only `union`.
        auto composePred = [&reg](const std::string& c, const std::string& f) {
            return reg.MergeTag(c, f) == "compose";
        };
        mergedById.emplace(id, ptrs.empty() ? *baseNode
                               : CB::engine::merge::bashMerge(*baseNode, ptrs, composePred));
    }

    // Nemesis symbol roster: variable/event NAME -> merged index, from the MERGED hkbBehaviorGraphStringData
    // (now the UNIONED roster across all mods, so a name only one mod added still resolves). Bindings /
    // transitions written as $variableID[Name]$ / $eventID[Name]$ resolve against it in pass 2. First
    // string-data wins (the graph's own). Built BEFORE pass 2 so it's ready when a binding field is parsed.
    SymTab syms;
    for (const std::string& id : order) {
        const CB::engine::common::xml::Node& n = mergedById[id];
        if (std::string(n.attr("class")) != "hkbBehaviorGraphStringData") continue;
        auto fill = [&](const char* param, std::unordered_map<std::string, int>& m) {
            if (const CB::engine::common::xml::Node* p = paramByName(n, param)) {
                int idx = 0;
                for (const auto& c : p->children) if (c.tag == "hkcstring") { m[c.text] = idx; ++idx; }
            }
        };
        fill("variableNames", syms.var);
        fill("eventNames",    syms.evt);
        break;
    }

    // Pass 2 — fill each object from its merged node ONCE (the union already applied; the class matches
    // the object by construction, so no per-layer drift skip is needed here anymore).
    for (auto& [id, so] : work) {
        fillObject(*so, mergedById[id], reg, idMap, &syms, err);
        if (!err.empty()) return false;
    }
    return true;
}

} // namespace

// ── ParseHky — the reflective .hky -> SchemaObject graph (the exact inverse of EmitHkyReflective) ─────────────
namespace {
namespace yml = c4::yml;

struct HkyParse {
    const schema::SchemaRegistry&                                          reg;
    const std::vector<std::string>*                                        bones;
    std::unordered_map<std::string, std::shared_ptr<io::SchemaObject>>     byId;     ///< file nodes + the root
    std::vector<std::shared_ptr<io::SchemaObject>>                         all;      ///< every object (owns them)
    struct Symbol { io::SchemaObject* obj; std::size_t field; bool isEvent; std::string name; std::string where; };
    std::vector<Symbol>                                                    symbols;  ///< name companions, resolved last
    std::string                                                            err;
    std::string                                                            where;    ///< "<file>" for messages
    bool                                                                   patch = false;   ///< fill as a FIELD PATCH
    std::function<std::string(const std::string&)>                         canon;   ///< id -> canonical key (null = verbatim)
    std::unordered_set<const IHavokObject*>                                nodes;   ///< file / shared / root objects
    std::map<std::pair<const IHavokObject*, std::size_t>, std::vector<std::string>> subKeys;   ///< minted roster keys
    std::function<std::string(const std::string&)>                         baseCanon;   ///< a stored base key -> canonical
    /// @brief The object being filled whose `hkaSkeleton` pointers a nested `boneof:` field names its skeleton by.
    io::SchemaObject*                                                      boneOwner = nullptr;
    /// @brief A `boneof` bone name, resolved once every skeleton has its bones (the skeleton may fill after the mapper).
    struct BoneName { io::SchemaObject* obj; std::size_t field, elem; io::SchemaObject* owner; std::string skeletonField, name, where; };
    std::vector<BoneName>                                                  boneNames;
    /// @brief The hkbBindable being filled, and the member path from it to the object being filled ("bones:3/").
    io::SchemaObject*                                                      bindable = nullptr;
    std::string                                                            member;
    /// @brief A `{ charprop: <name> }` member, whose binding its bindable appends once its own fields are filled.
    struct CharPropBind { io::SchemaObject* owner; std::string path, name, where; };
    std::vector<CharPropBind>                                              charPropBinds;
};

/// @brief While filling the object behind `owner`'s field `f` (element `k`): an inline struct continues the member
///        path, a pointer leaves the bindable (a member path never crosses one).
struct MemberScope {
    HkyParse&         ps;
    io::SchemaObject* bindable;
    std::string       member;
    MemberScope(HkyParse& p, const Field& f, std::size_t k = 0) : ps(p), bindable(p.bindable), member(p.member) {
        if (f.kind == FieldKind::Struct) ps.member = member + f.name + "/";
        else if (f.kind == FieldKind::StructArray) ps.member = member + f.name + ":" + std::to_string(k) + "/";
        else { ps.bindable = nullptr; ps.member.clear(); }
    }
    ~MemberScope() { ps.bindable = bindable; ps.member = member; }
};

/// @brief The merge tag for `field`, looked up on the class that DECLARES it (MergeTag consults own fields only).
std::string mergeTagOf(const schema::SchemaRegistry& reg, std::string cls, const std::string& field) {
    for (int d = 0; d < 64 && !cls.empty(); ++d) {
        if (std::string t = reg.MergeTag(cls, field); !t.empty()) return t;
        const schema::ClassSchema* cs = reg.Find(cls);
        if (!cs) break;
        cls = cs->parent;
    }
    return {};
}

std::string ystr(const yml::ConstNodeRef& n) {
    std::string s;
    if (n.readable() && n.has_val()) c4::from_chars(n.val(), &s);
    return s;
}
bool yhas(const yml::ConstNodeRef& n, const std::string& k) {
    return n.readable() && n.is_map() && n.has_child(c4::to_csubstr(k));
}
yml::ConstNodeRef yget(const yml::ConstNodeRef& n, const std::string& k) { return n[c4::to_csubstr(k)]; }

/// @note strtof per token (not istringstream): NaN / inf spellings (`nan`, `-nan(ind)`, `inf`) must parse — a
///       stream extraction fails on them and silently leaves zeros.
/// @brief Float text -> bits: `nan_<8 hex>` is raw NaN bits (the emitter's bit-exact NaN form), else strtof.
std::vector<std::uint8_t> floatBytes(const std::string& t) {
    std::vector<std::uint8_t> r(4, 0);
    if (t.rfind("nan_", 0) == 0) {
        const std::uint32_t u = static_cast<std::uint32_t>(std::strtoul(t.c_str() + 4, nullptr, 16));
        std::memcpy(r.data(), &u, 4);
        return r;
    }
    const float f = std::strtof(t.c_str(), nullptr);
    std::memcpy(r.data(), &f, 4);
    return r;
}
std::vector<std::uint8_t> vec4Strict(const std::string& text) {
    std::string s = text;
    for (char& c : s) if (c == '(' || c == ')' || c == ',' || c == '\'') c = ' ';
    std::vector<std::uint8_t> r(16, 0);
    std::size_t i = 0;
    for (int k = 0; k < 4; ++k) {
        while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n')) ++i;
        if (i >= s.size()) break;
        std::size_t e = i;
        while (e < s.size() && s[e] != ' ' && s[e] != '\t' && s[e] != '\n') ++e;
        const auto b = floatBytes(s.substr(i, e - i));
        std::memcpy(r.data() + k * 4, b.data(), 4);
        i = e;
    }
    return r;
}

/// @brief Scalar text -> little-endian bytes. Enum/flag names resolve through the schema-generated tables; an
///        unknown name is an ERROR (never a silent 0).
bool scalarBytes(const Field& f, const std::string& text, std::vector<std::uint8_t>& out, HkyParse& ps) {
    if (f.scalar == Scalar::Float) { out = floatBytes(text); return true; }
    if (f.scalar == Scalar::Bool)  { out = encInt(text == "true" ? 1 : 0, Scalar::Bool); return true; }
    std::int64_t v = 0;
    if (!f.enumName.empty()) {
        const auto& t = en::EnumTable(f.enumName);
        std::size_t s = 0;
        while (s <= text.size()) {
            std::size_t e = text.find('|', s);
            if (e == std::string::npos) e = text.size();
            std::string tok = text.substr(s, e - s);
            tok.erase(0, tok.find_first_not_of(' '));
            if (const auto l = tok.find_last_not_of(' '); l != std::string::npos) tok.erase(l + 1);
            if (!tok.empty()) {
                if (auto it = t.find(tok); it != t.end()) v |= it->second;
                else {
                    char* end = nullptr;
                    const long long n = std::strtoll(tok.c_str(), &end, 0);
                    if (!end || *end) { ps.err = ps.where + ": " + f.name + ": unknown " + f.enumName + " value '" + tok + "'"; return false; }
                    v |= n;
                }
            }
            s = e + 1;
        }
    } else {
        char* end = nullptr;
        v = (!text.empty() && text[0] == '-') ? std::strtoll(text.c_str(), &end, 0)
                                                : static_cast<std::int64_t>(std::strtoull(text.c_str(), &end, 0));
        if (!end || *end) { ps.err = ps.where + ": " + f.name + ": not a number '" + text + "'"; return false; }
    }
    out = encInt(v, f.scalar);
    return true;
}

/// @brief A bone reference: a rig name (resolved against the graph's rig) or a raw index.
bool boneIndex(const std::string& text, std::int64_t& out, HkyParse& ps, const std::string& field) {
    char* end = nullptr;
    const long long n = std::strtoll(text.c_str(), &end, 10);
    if (!text.empty() && end && !*end) { out = n; return true; }
    if (ps.bones)
        for (std::size_t i = 0; i < ps.bones->size(); ++i)
            if ((*ps.bones)[i] == text) { out = static_cast<std::int64_t>(i); return true; }
    ps.err = ps.where + ": " + field + ": bone '" + text + "' is not in the graph's rig";
    return false;
}

bool fillHky(io::SchemaObject& so, const yml::ConstNodeRef& map, HkyParse& ps);
bool fillFields(io::SchemaObject& so, const yml::ConstNodeRef& map, HkyParse& ps);
bool fieldValue(io::SchemaObject& so, std::size_t i, const yml::ConstNodeRef& n, HkyParse& ps);

/// @brief One pointer / struct value: a file-node id, an inline map (optionally `class:`-tagged), or the `unwrap:`
///        collapsed form of the declared class.
bool objectValue(const yml::ConstNodeRef& n, const std::string& declared, std::shared_ptr<IHavokObject>& out, HkyParse& ps,
                 const std::string& field) {
    if (n.has_val() && !n.is_map() && !n.is_seq()) {
        const std::string r = ystr(n);
        if (r == "null") { out = nullptr; return true; }
        auto it = ps.byId.find(ps.canon ? ps.canon(r) : r);
        if (it == ps.byId.end()) { ps.err = ps.where + ": " + field + ": no node with id '" + r + "'"; return false; }
        out = it->second;
        return true;
    }
    const schema::ClassSchema* dcs = ps.reg.Find(declared);
    std::string cls = declared;
    if (n.is_map() && yhas(n, "class")) cls = ystr(yget(n, "class"));
    auto obj = makeObj(ps.reg, cls);
    if (!obj) { ps.err = ps.where + ": " + field + ": unknown class '" + cls + "'"; return false; }
    ps.all.push_back(obj);
    out = obj;
    // `unwrap:` — the value IS the declared class's single field: a seq, or a NON-EMPTY map naming none of the
    // class's fields. `{}` always means an empty object (the emitter never collapses to `{}`).
    if (dcs && !dcs->unwrap.empty() && !(n.is_map() && yhas(n, "class"))) {
        bool collapsed = n.is_seq();
        if (n.is_map() && n.num_children() > 0) {
            collapsed = true;
            for (const Field* f : obj->Fields())
                if (!f->name.empty() && yhas(n, f->name)) { collapsed = false; break; }
        }
        if (collapsed) {
            const auto fields = obj->Fields();
            for (std::size_t i = 0; i < fields.size(); ++i)
                if (fields[i]->name == dcs->unwrap) return fieldValue(*obj, i, n, ps);
        }
    }
    return n.is_map() ? fillHky(*obj, n, ps) : true;
}

std::string ykey(const yml::ConstNodeRef& e) { std::string s; c4::from_chars(e.key(), &s); return s; }

/// @brief One roster entry built WHOLE (not patched): a `ref` key IS the child node; other kinds build the element
///        from the entry's value, then restore the key field the emitter left out of the body.
bool rosterElement(const yml::ConstNodeRef& e, const Field& f, const std::string& kind, const std::string& keyField,
                   const std::string& key, std::shared_ptr<IHavokObject>& out, HkyParse& ps) {
    if (kind == "ref") {
        const auto it = ps.byId.find(ps.canon ? ps.canon(key) : key);
        if (it == ps.byId.end()) { ps.err = ps.where + ": " + f.name + ": no node with id '" + key + "'"; return false; }
        out = it->second;
        return true;
    }
    const bool was = ps.patch;
    ps.patch = false;
    const bool ok = objectValue(e, f.ref, out, ps, f.name);
    ps.patch = was;
    if (!ok || (kind != "field" && kind != "bone")) return ok;
    auto* so = dynamic_cast<io::SchemaObject*>(out.get());
    if (!so) { ps.err = ps.where + ": " + f.name + ": roster entry '" + key + "' must be an inline " + f.ref; return false; }
    const auto ef = so->Fields();
    for (std::size_t j = 0; j < ef.size(); ++j) {
        if (ef[j]->name != keyField) continue;
        if (kind == "field") { so->FieldAt(j).str = key; return true; }
        std::int64_t idx = -1;
        if (!boneIndex(key, idx, ps, f.name)) return false;
        so->FieldAt(j).raw = encInt(idx, ef[j]->scalar);
        return true;
    }
    ps.err = ps.where + ": " + f.name + ": " + f.ref + " has no key field '" + keyField + "'";
    return false;
}

/// @brief A keyed roster (`roster:` field written as a map), built whole in list order; minted keys are kept.
bool rosterValue(io::SchemaObject& so, std::size_t i, const yml::ConstNodeRef& n, HkyParse& ps) {
    const Field& f = *so.Fields()[i];
    const auto [kind, keyField] = rosterKind(f);
    std::vector<std::string> keys;
    for (auto e : n) {
        const std::string key = ykey(e);
        const MemberScope scope(ps, f, keys.size());
        std::shared_ptr<IHavokObject> o;
        if (!rosterElement(e, f, kind, keyField, key, o, ps)) return false;
        so.FieldAt(i).objs.push_back(o);
        keys.push_back(ps.canon ? ps.canon(key) : key);
    }
    if (kind == "id" || kind == "slot") ps.subKeys[{ &so, i }] = std::move(keys);
    return true;
}

bool fieldValue(io::SchemaObject& so, std::size_t i, const yml::ConstNodeRef& n, HkyParse& ps) {
    const Field& f = *so.Fields()[i];
    io::FieldValue& v = so.FieldAt(i);
    v = io::FieldValue{};
    if ((f.kind == FieldKind::PtrArray || f.kind == FieldKind::StructArray) && !f.roster.empty() && n.is_map())
        return rosterValue(so, i, n, ps);
    /// @note A quoted `boneof` value is a bone name, held as index 0 until the skeletons are built; a bare one is the index.
    const auto boneOfName = [&](const yml::ConstNodeRef& e, std::size_t elem) {
        if (f.boneOf.empty() || !e.is_val_quoted()) return false;
        ps.boneNames.push_back({ &so, i, elem, ps.boneOwner, f.boneOf, ystr(e), ps.where });
        return true;
    };
    switch (f.kind) {
        case FieldKind::Scalar: {
            if (f.user && !n.is_seq() && !n.is_val_quoted() && ystr(n) == "user") { v.raw = encInt(f.userValue, f.scalar); return true; }
            if (!n.is_seq() && boneOfName(n, 0)) { v.raw = encInt(0, f.scalar); return true; }
            if (f.boneRef && !n.is_seq()) {
                std::int64_t idx = -1;
                if (!boneIndex(ystr(n), idx, ps, f.name)) return false;
                v.raw = encInt(idx, f.scalar);
                return true;
            }
            if (n.is_seq()) {
                for (auto e : n) { std::vector<std::uint8_t> b; if (!scalarBytes(f, ystr(e), b, ps)) return false; v.raw.insert(v.raw.end(), b.begin(), b.end()); }
                return true;
            }
            return scalarBytes(f, ystr(n), v.raw, ps);
        }
        case FieldKind::String:
        case FieldKind::CString:    v.str = ystr(n); return true;
        case FieldKind::Vector4:
        case FieldKind::Quaternion:
            if (!n.is_seq()) { v.raw = vec4Strict(ystr(n)); return true; }
            for (auto e : n) { const auto b = vec4Strict(ystr(e)); v.raw.insert(v.raw.end(), b.begin(), b.end()); }
            return true;
        case FieldKind::QsTransform: {
            const bool many = n.is_seq() && n.num_children() > 0 && n.first_child().is_seq();
            for (auto e : n) {
                if (many) for (auto q : e) { const auto b = vec4Strict(ystr(q)); v.raw.insert(v.raw.end(), b.begin(), b.end()); }
                else { const auto b = vec4Strict(ystr(e)); v.raw.insert(v.raw.end(), b.begin(), b.end()); }
            }
            return true;
        }
        case FieldKind::Ptr: {
            const MemberScope scope(ps, f);
            if (f.count > 1 && n.is_seq()) {
                for (auto e : n) {
                    std::shared_ptr<IHavokObject> o;
                    if (!objectValue(e, f.ref, o, ps, f.name)) return false;
                    v.objs.push_back(o);
                }
                return true;
            }
            return objectValue(n, f.ref, v.obj, ps, f.name);
        }
        case FieldKind::Struct: {
            const MemberScope scope(ps, f);
            return objectValue(n, f.ref, v.obj, ps, f.name);
        }
        case FieldKind::PtrArray:
        case FieldKind::StructArray: {
            std::size_t k = 0;
            for (auto e : n) {
                const MemberScope scope(ps, f, k++);
                std::shared_ptr<IHavokObject> o;
                if (!objectValue(e, f.ref, o, ps, f.name)) return false;
                v.objs.push_back(o);
            }
            return true;
        }
        case FieldKind::StringArray:
            for (auto e : n) v.strs.push_back(ystr(e));
            return true;
        case FieldKind::ScalarArray: {
            const int w = schema::ScalarWidth(f.scalar);
            if (f.boneWeights && n.is_map()) {
                std::string dflt = f.boneWeightDefault;
                if (yhas(n, "default")) dflt = ystr(yget(n, "default"));
                std::size_t count = ps.bones ? ps.bones->size() : 0;
                if (yhas(n, "count")) count = static_cast<std::size_t>(std::strtoull(ystr(yget(n, "count")).c_str(), nullptr, 10));
                else if (!ps.bones) { ps.err = ps.where + ": " + f.name + ": a weight map needs the graph's rig (or a count:)"; return false; }
                std::vector<std::uint8_t> d = floatBytes(dflt);
                for (std::size_t k = 0; k < count; ++k) v.raw.insert(v.raw.end(), d.begin(), d.end());
                for (auto e : n) {
                    const std::string key = [&] { std::string s; c4::from_chars(e.key(), &s); return s; }();
                    if (key == "default" || key == "count") continue;
                    std::int64_t idx = -1;
                    if (!boneIndex(key, idx, ps, f.name)) return false;
                    if (idx < 0 || static_cast<std::size_t>(idx) >= count) { ps.err = ps.where + ": " + f.name + ": bone '" + key + "' is past count " + std::to_string(count); return false; }
                    const auto b = floatBytes(ystr(e));
                    std::memcpy(v.raw.data() + idx * 4, b.data(), 4);
                }
                return true;
            }
            for (auto e : n) {
                std::vector<std::uint8_t> b;
                if (boneOfName(e, v.raw.size() / static_cast<std::size_t>(w))) b = encInt(0, f.scalar);
                else if (f.boneList) { std::int64_t idx = -1; if (!boneIndex(ystr(e), idx, ps, f.name)) return false; b = encInt(idx, f.scalar); }
                else if (!scalarBytes(f, ystr(e), b, ps)) return false;
                b.resize(static_cast<std::size_t>(w));
                v.raw.insert(v.raw.end(), b.begin(), b.end());
            }
            return true;
        }
        case FieldKind::Vec4Array:
            for (auto e : n) { const auto b = vec4Strict(ystr(e)); v.raw.insert(v.raw.end(), b.begin(), b.end()); }
            return true;
        case FieldKind::QsTransformArray:
            for (auto e : n) for (auto q : e) { const auto b = vec4Strict(ystr(q)); v.raw.insert(v.raw.end(), b.begin(), b.end()); }
            return true;
        case FieldKind::BoolArray:
            for (auto e : n) v.raw.push_back(ystr(e) == "true" ? 1 : 0);
            return true;
        default: return true;
    }
}

/// @brief Fill every schema field present in `map`. Absent fields keep their rendered-absent meaning: a null
///        pointer / empty array (Init default), -1 for a `boneref`, a default object for an inline struct.
/// @brief Enum / flags text -> value through the schema-generated table; an unknown name is an error.
bool enumParse(const char* table, const std::string& text, long& out, HkyParse& ps, const std::string& what) {
    const auto& t = en::EnumTable(table);
    out = 0;
    std::size_t s = 0;
    while (s <= text.size()) {
        std::size_t e = text.find('|', s);
        if (e == std::string::npos) e = text.size();
        std::string tok = text.substr(s, e - s);
        tok.erase(0, tok.find_first_not_of(' '));
        if (const auto l = tok.find_last_not_of(' '); l != std::string::npos) tok.erase(l + 1);
        if (!tok.empty()) {
            if (auto it = t.find(tok); it != t.end()) out |= it->second;
            else {
                char* end = nullptr;
                const long long n = std::strtoll(tok.c_str(), &end, 0);
                if (!end || *end) { ps.err = ps.where + ": " + what + ": unknown " + table + " value '" + tok + "'"; return false; }
                out |= static_cast<long>(n);
            }
        }
        s = e + 1;
    }
    return true;
}

/// @brief The inline object behind pointer field `field`, created (declared class) when absent.
io::SchemaObject* ensurePtr(io::SchemaObject& so, const char* field, HkyParse& ps) {
    const auto j = so.IndexOf(field);
    if (!j) return nullptr;
    io::FieldValue& fv = so.FieldAt(*j);
    if (!fv.obj) {
        auto o = makeObj(ps.reg, so.Fields()[*j]->ref);
        if (!o) return nullptr;
        ps.all.push_back(o);
        fv.obj = o;
    }
    return dynamic_cast<io::SchemaObject*>(fv.obj.get());
}

/// @brief One variable / character-property record onto `info` (+ its word, and its quad for vector types). `fresh`
///        = a new entry (needs `type:`); an existing one keeps what its record leaves out.
bool applyInfoRecord(io::SchemaObject& info, const yml::ConstNodeRef& rec, bool fresh, io::SchemaObject* word,
                     std::vector<std::uint8_t>& quads, std::vector<std::shared_ptr<IHavokObject>>* variants,
                     const std::string& what, HkyParse& ps) {
    auto make = [&](const char* cls) { auto o = makeObj(ps.reg, cls); ps.all.push_back(o); return o; };
    auto storage = [](long t) { return isPointerType(t) ? 2 : isQuadType(t) ? 1 : 0; };
    {
        long type = static_cast<long>(info.GetInt("type"));
        if (yhas(rec, "type")) {
            long t = 0;
            if (!enumParse("VariableType", ystr(yget(rec, "type")), t, ps, what)) return false;
            if (!fresh && storage(t) != storage(type)) { ps.err = ps.where + ": " + what + ": changing a variable's storage (word / quad / pointer) is not supported"; return false; }
            type = t;
            info.SetInt("type", static_cast<std::int8_t>(type));
        } else if (fresh) { ps.err = ps.where + ": " + what + ": a new entry needs `type:`"; return false; }
        io::FieldValue& rf = info.FieldRef("role");
        if (!rf.obj) rf.obj = make("hkbRoleAttribute");
        auto* role = dynamic_cast<io::SchemaObject*>(rf.obj.get());
        if (yhas(rec, "role"))  { long r = 0;  if (!enumParse("Role", ystr(yget(rec, "role")), r, ps, what)) return false; role->SetInt("role", static_cast<std::int16_t>(r)); }
        if (yhas(rec, "flags")) { long fl = 0; if (!enumParse("RoleFlags", ystr(yget(rec, "flags")), fl, ps, what)) return false; role->SetInt("flags", static_cast<std::int16_t>(fl)); }
        if (!word) return true;
        if (isPointerType(type)) {
            if (!variants) { ps.err = ps.where + ": " + what + ": a pointer value needs a value set"; return false; }
            std::size_t vi = variants->size();
            if (fresh) { word->SetInt("value", static_cast<std::int32_t>(vi)); variants->push_back(nullptr); }
            else vi = static_cast<std::size_t>(word->GetInt("value"));
            if (yhas(rec, "value") && vi < variants->size()) {
                /// @note A PATCH record onto an existing inline value of the same class (a bone-weight preset) patches
                ///       it in place — per bone — so two mods adjusting different bones of `UpperBody` both land.
                const yml::ConstNodeRef val = yget(rec, "value");
                auto* co = dynamic_cast<io::SchemaObject*>((*variants)[vi].get());
                if (!fresh && ps.patch && val.is_map() && co && !ps.nodes.count(co) &&
                    (!yhas(val, "class") || ystr(yget(val, "class")) == co->ClassName()))
                    return fillHky(*co, val, ps);
                std::shared_ptr<IHavokObject> o;
                const bool was = ps.patch;
                ps.patch = false;
                const bool ok = objectValue(val, "hkReferencedObject", o, ps, what);
                ps.patch = was;
                if (!ok) return false;
                (*variants)[vi] = o;
            }
            return true;
        }
        if (isQuadType(type)) {
            std::size_t qi = quads.size() / 16;
            if (fresh) { word->SetInt("value", static_cast<std::int32_t>(qi)); quads.resize(quads.size() + 16, 0); }
            else qi = static_cast<std::size_t>(word->GetInt("value"));
            if (yhas(rec, "value") && qi * 16 + 16 <= quads.size()) {
                const auto b = vec4Strict(ystr(yget(rec, "value")));
                std::memcpy(quads.data() + qi * 16, b.data(), 16);
            }
            return true;
        }
        if (yhas(rec, "value")) {
            const std::string t = ystr(yget(rec, "value"));
            if (type == 4) word->FieldRef("value").raw = floatBytes(t);
            else if (t == "true" || t == "false") word->SetInt("value", t == "true" ? 1 : 0);
            else {
                char* end = nullptr;
                const long long n = std::strtoll(t.c_str(), &end, 0);
                if (!end || *end) { ps.err = ps.where + ": " + what + ": value '" + t + "' is not a number"; return false; }
                word->SetInt("value", static_cast<std::int32_t>(n));
            }
        }
        return true;
    }
}

/// @brief Build (full) or patch (ps.patch) a VALUED roster from its name-keyed records: a new name appends to the
///        names, infos and word values (a vector type also appends its quad and takes the running quad index); an
///        existing name updates only what its record states.
bool fillValued(io::SchemaObject& owner, const ValuedRoster& r, const yml::ConstNodeRef* entries, HkyParse& ps) {
    io::SchemaObject* sd = ensurePtr(owner, "stringData", ps);
    io::SchemaObject* vs = ensurePtr(owner, r.values, ps);
    if (!sd || !vs) { ps.err = ps.where + ": " + r.key + ": vocabulary objects missing"; return false; }
    auto& names = sd->FieldRef(r.names).strs;
    auto& infos = owner.FieldRef(r.infos).objs;
    auto& words = vs->FieldRef("wordVariableValues").objs;
    auto& quads = vs->FieldRef("quadVariableValues").raw;
    auto& variants = vs->FieldRef("variantVariableValues").objs;
    if (!ps.patch) { names.clear(); infos.clear(); words.clear(); quads.clear(); variants.clear(); }
    if (!entries) return true;
    for (auto e : *entries) {
        const std::string name = ykey(e);
        const std::string what = std::string(r.key) + " '" + name + "'";
        const auto it = std::find(names.begin(), names.end(), name);
        if (it == names.end()) {
            auto info = makeObj(ps.reg, "hkbVariableInfo"), word = makeObj(ps.reg, "hkbVariableValue");
            ps.all.push_back(info);
            ps.all.push_back(word);
            names.push_back(name); infos.push_back(info); words.push_back(word);
            if (!applyInfoRecord(*info, e, true, word.get(), quads, &variants, what, ps)) return false;
        } else {
            const std::size_t k = static_cast<std::size_t>(it - names.begin());
            if (!applyInfoRecord(*dynamic_cast<io::SchemaObject*>(infos[k].get()), e, false,
                                 dynamic_cast<io::SchemaObject*>(words[k].get()), quads, &variants, what, ps)) return false;
        }
    }
    return true;
}

/// @brief Build (full) or patch (ps.patch) a `vocab: graph` graph-data object from its name-keyed records (see
///        fillValued; the unvalued rosters append the same way).
/// @note A `union` string roster (diffObject) likewise only ever ADDS names: names are identity there, so a mod
///       never replaces or reorders one.
bool fillVocab(io::SchemaObject& gd, const yml::ConstNodeRef& map, HkyParse& ps) {
    io::SchemaObject* sd = ensurePtr(gd, "stringData", ps);
    if (!sd) { ps.err = ps.where + ": vocabulary objects missing"; return false; }
    const yml::ConstNodeRef vars = yhas(map, "variables") ? yget(map, "variables") : yml::ConstNodeRef();
    if (!fillValued(gd, kGraphVariables, yhas(map, "variables") ? &vars : nullptr, ps)) return false;
    auto& cNames = sd->FieldRef("characterPropertyNames").strs;
    auto& eNames = sd->FieldRef("eventNames").strs;
    auto& aNames = sd->FieldRef("attributeNames").strs;
    auto& cInfos = gd.FieldRef("characterPropertyInfos").objs;
    auto& eInfos = gd.FieldRef("eventInfos").objs;
    auto& aDefs  = gd.FieldRef("attributeDefaults").raw;
    if (!ps.patch) { cNames.clear(); eNames.clear(); aNames.clear(); cInfos.clear(); eInfos.clear(); aDefs.clear(); }
    auto nameOf = [](const yml::ConstNodeRef& e) { return ykey(e); };
    auto make = [&](const char* cls) { auto o = makeObj(ps.reg, cls); ps.all.push_back(o); return o; };
    std::vector<std::uint8_t> noQuads;
    if (yhas(map, "characterProperties"))
        for (auto e : yget(map, "characterProperties")) {
            const std::string name = nameOf(e);
            const std::string what = "character property '" + name + "'";
            const auto it = std::find(cNames.begin(), cNames.end(), name);
            if (it == cNames.end()) {
                auto info = make("hkbVariableInfo");
                cNames.push_back(name); cInfos.push_back(info);
                if (!applyInfoRecord(*info, e, true, nullptr, noQuads, nullptr, what, ps)) return false;
            } else if (!applyInfoRecord(*dynamic_cast<io::SchemaObject*>(cInfos[static_cast<std::size_t>(it - cNames.begin())].get()), e, false,
                                        nullptr, noQuads, nullptr, what, ps)) return false;
        }
    if (yhas(map, "events"))
        for (auto e : yget(map, "events")) {
            const std::string name = nameOf(e);
            auto it = std::find(eNames.begin(), eNames.end(), name);
            std::shared_ptr<io::SchemaObject> info;
            if (it == eNames.end()) { info = make("hkbEventInfo"); eNames.push_back(name); eInfos.push_back(info); }
            else info = std::dynamic_pointer_cast<io::SchemaObject>(eInfos[static_cast<std::size_t>(it - eNames.begin())]);
            if (yhas(e, "flags")) {
                long fl = 0;
                if (!enumParse("EventInfoFlags", ystr(yget(e, "flags")), fl, ps, "event '" + name + "'")) return false;
                info->SetInt("flags", static_cast<std::uint32_t>(fl));
            }
        }
    if (yhas(map, "attributes"))
        for (auto e : yget(map, "attributes")) {
            const std::string name = nameOf(e);
            const auto b = floatBytes(ystr(e));
            auto it = std::find(aNames.begin(), aNames.end(), name);
            if (it == aNames.end()) { aNames.push_back(name); aDefs.insert(aDefs.end(), b.begin(), b.end()); }
            else std::memcpy(aDefs.data() + 4 * static_cast<std::size_t>(it - aNames.begin()), b.data(), 4);
        }
    return true;
}

/// @brief A keyed roster in a PATCH: each entry names a sub-record by its key. A known key overrides that entry
///        (field-wise for an inline record); an unknown key appends a new entry; `{deleted: true}` removes one.
///        Keys compare CANONICALLY (patch keys through the patch's masters, stored base keys through the base's).
/// @note A `slot` roster's positions are referenced elsewhere (hkbManualSelectorGenerator.selectedGeneratorIndex),
///       so deleting a slot would silently re-point every later index — refused.
bool rosterPatch(io::SchemaObject& so, std::size_t i, const yml::ConstNodeRef& n, HkyParse& ps) {
    const Field& f = *so.Fields()[i];
    const auto [kind, keyField] = rosterKind(f);
    io::FieldValue& v = so.FieldAt(i);
    const bool minted = kind == "id" || kind == "slot";
    std::vector<std::string>* keys = nullptr;
    if (minted) {
        keys = &ps.subKeys[{ &so, i }];
        if (keys->size() != v.objs.size()) {
            keys->clear();
            for (std::size_t k = 0; k < v.objs.size(); ++k) keys->push_back(mintedKey(k));
        }
    }
    auto stored = [&](const std::string& s) { return s.find(':') != std::string::npos || !ps.baseCanon ? s : ps.baseCanon(s); };
    auto elemKey = [&](const std::shared_ptr<IHavokObject>& p) -> std::string {
        const auto* e = dynamic_cast<const io::SchemaObject*>(p.get());
        if (!e) return {};
        const auto ef = e->Fields();
        for (std::size_t j = 0; j < ef.size(); ++j) {
            if (ef[j]->name != keyField) continue;
            if (kind == "field") return e->Values()[j].str;
            const std::int64_t bi = decodeInt(e->Values()[j].raw, ef[j]->scalar);
            return ps.bones && bi >= 0 && bi < static_cast<std::int64_t>(ps.bones->size()) ? (*ps.bones)[static_cast<std::size_t>(bi)] : std::string();
        }
        return {};
    };
    for (auto e : n) {
        const std::string key = ykey(e);
        const std::string ck = ps.canon ? ps.canon(key) : key;
        std::size_t j = v.objs.size();
        if (kind == "ref") {
            if (const auto it = ps.byId.find(ck); it != ps.byId.end())
                for (std::size_t k = 0; k < v.objs.size(); ++k) if (v.objs[k] == it->second) { j = k; break; }
        } else if (minted) {
            for (std::size_t k = 0; k < keys->size(); ++k) if (stored((*keys)[k]) == ck) { j = k; break; }
        } else {
            for (std::size_t k = 0; k < v.objs.size(); ++k) if (elemKey(v.objs[k]) == key) { j = k; break; }
        }
        if (e.is_map() && yhas(e, "deleted")) {
            if (j == v.objs.size()) continue;
            if (kind == "slot") { ps.err = ps.where + ": " + f.name + ": slot '" + key + "' is referenced by position and can't be deleted"; return false; }
            v.objs.erase(v.objs.begin() + static_cast<std::ptrdiff_t>(j));
            if (keys) keys->erase(keys->begin() + static_cast<std::ptrdiff_t>(j));
            continue;
        }
        if (j < v.objs.size()) {
            if (kind == "ref") continue;
            auto* co = dynamic_cast<io::SchemaObject*>(v.objs[j].get());
            const bool sameClass = !(e.is_map() && yhas(e, "class")) || (co && ystr(yget(e, "class")) == co->ClassName());
            if (e.is_map() && co && !ps.nodes.count(co) && sameClass) {
                if (!fillHky(*co, e, ps)) return false;
                continue;
            }
            std::shared_ptr<IHavokObject> o;
            if (!rosterElement(e, f, kind, keyField, key, o, ps)) return false;
            v.objs[j] = o;
            continue;
        }
        std::shared_ptr<IHavokObject> o;
        if (!rosterElement(e, f, kind, keyField, key, o, ps)) return false;
        v.objs.push_back(o);
        if (keys) keys->push_back(ck);
    }
    return true;
}

/// @brief FIELD-PATCH semantics for one present field (`handled` = done; else the caller replaces the value WHOLE):
///        a keyed roster merges by key (rosterPatch); a map onto an existing inline object of the same class merges
///        field-wise (recursively); a `union` name roster only ADDS the names it lacks. Every other value — any list
///        without a roster — is replaced whole: the last mod to change it wins.
bool patchField(io::SchemaObject& so, std::size_t i, const yml::ConstNodeRef& n, HkyParse& ps, bool& handled) {
    handled = false;
    const Field& f = *so.Fields()[i];
    io::FieldValue& v = so.FieldAt(i);
    if ((f.kind == FieldKind::PtrArray || f.kind == FieldKind::StructArray) && !f.roster.empty() && n.is_map()) {
        handled = true;
        return rosterPatch(so, i, n, ps);
    }
    if ((f.kind == FieldKind::Ptr || f.kind == FieldKind::Struct) && n.is_map() && n.num_children() > 0 && v.obj) {
        auto* co = dynamic_cast<io::SchemaObject*>(v.obj.get());
        /// @note In a patch, an absent `class:` means "the class already there" — a polymorphic pointer (declared as a
        ///       base class) must not read as a class change and be replaced by a stub of the declared class.
        const std::string cls = yhas(n, "class") ? ystr(yget(n, "class")) : (co ? std::string(co->ClassName()) : f.ref);
        bool collapsed = false;
        if (const schema::ClassSchema* dcs = ps.reg.Find(f.ref); co && dcs && !dcs->unwrap.empty()) {
            collapsed = true;
            for (const Field* g : co->Fields()) if (!g->name.empty() && yhas(n, g->name)) { collapsed = false; break; }
        }
        if (co && !ps.nodes.count(co) && cls == co->ClassName() && !collapsed) {
            handled = true;
            return fillHky(*co, n, ps);   // ps.patch stays set: the nested object merges field-wise too
        }
        /// @note A COLLAPSED patch (the `unwrap:` field's value right under this field — a weight map's changed bones)
        ///       patches that field of the existing object; only a form the field can't patch replaces it whole.
        if (co && !ps.nodes.count(co) && collapsed) {
            const auto cf = co->Fields();
            const std::string& u = ps.reg.Find(f.ref)->unwrap;
            for (std::size_t j = 0; j < cf.size(); ++j) {
                if (cf[j]->name != u) continue;
                bool h = false;
                if (!patchField(*co, j, n, ps, h)) return false;
                if (!h && !fieldValue(*co, j, n, ps)) return false;
                handled = true;
                return true;
            }
        }
    }
    /// @note Bones merge PER BONE: a weight map in a patch sets only the bones it names (a mod never restates another
    ///       mod's weights); a bone set adds `'Bone': {}` / removes `'Bone': {deleted: true}`. A weight map carrying
    ///       `default:` / `count:` is a whole value (it resizes or re-bases the array).
    if (f.kind == FieldKind::ScalarArray && n.is_map() && f.boneWeights && f.scalar == Scalar::Float &&
        !yhas(n, "default") && !yhas(n, "count")) {
        handled = true;
        for (auto e : n) {
            const std::string bone = ykey(e);
            std::int64_t idx = -1;
            if (!boneIndex(bone, idx, ps, f.name)) return false;
            if (idx < 0 || static_cast<std::size_t>(idx) * 4 + 4 > v.raw.size()) {
                ps.err = ps.where + ": " + f.name + ": bone '" + bone + "' is past the weight array (" + std::to_string(v.raw.size() / 4) + ")";
                return false;
            }
            const auto wb = floatBytes(ystr(e));
            std::memcpy(v.raw.data() + static_cast<std::size_t>(idx) * 4, wb.data(), 4);
        }
        return true;
    }
    if (f.kind == FieldKind::ScalarArray && n.is_map() && f.boneSet) {
        handled = true;
        const std::size_t w = static_cast<std::size_t>(schema::ScalarWidth(f.scalar));
        for (auto e : n) {
            const std::string bone = ykey(e);
            std::int64_t idx = -1;
            if (!boneIndex(bone, idx, ps, f.name)) return false;
            std::size_t at = v.raw.size() / w;
            for (std::size_t k = 0; w && k < v.raw.size() / w; ++k) if (decodeInt(slice(v.raw, k, w), f.scalar) == idx) { at = k; break; }
            const bool present = at < v.raw.size() / w;
            if (e.is_map() && yhas(e, "deleted")) {
                if (present) v.raw.erase(v.raw.begin() + static_cast<std::ptrdiff_t>(at * w), v.raw.begin() + static_cast<std::ptrdiff_t>(at * w + w));
            } else if (!present) {
                auto b = encInt(idx, f.scalar);
                b.resize(w);
                v.raw.insert(v.raw.end(), b.begin(), b.end());
            }
        }
        return true;
    }
    if (f.kind == FieldKind::StringArray && n.is_seq() && mergeTagOf(ps.reg, so.ClassName(), f.name) == "union") {
        handled = true;
        for (auto e : n) {
            const std::string s = ystr(e);
            if (std::find(v.strs.begin(), v.strs.end(), s) == v.strs.end()) v.strs.push_back(s);
        }
    }
    return true;
}

/// @brief `{ charprop: <name> }` on `so`'s `charprop` field `i`: the member takes its neutral value and its bindable
///        owes a character-property binding to `<name>`.
bool charPropValue(io::SchemaObject& so, std::size_t i, const std::string& name, HkyParse& ps) {
    const Field& f = *so.Fields()[i];
    io::FieldValue& v = so.FieldAt(i);
    v = io::FieldValue{};
    if (!ps.bindable) { ps.err = ps.where + ": " + f.name + ": a charprop member outside any bindable"; return false; }
    if (name.empty()) { ps.err = ps.where + ": " + f.name + ": charprop names no property"; return false; }
    if (f.kind == FieldKind::Scalar) v.raw = encInt(-1, f.scalar);
    else {
        auto o = makeObj(ps.reg, f.charProp);
        if (!o) { ps.err = ps.where + ": " + f.name + ": unknown class '" + f.charProp + "'"; return false; }
        ps.all.push_back(o);
        v.obj = o;
    }
    ps.charPropBinds.push_back({ ps.bindable, ps.member + f.name, name, ps.where });
    return true;
}

/// @brief Append the bindings `so`'s `charprop` members owe, in walk order, after its own: where vanilla keeps them. A
///        binding already on that member path is replaced (a patch re-pointing a member).
/// @pre Every file of the unit is read: a node's bindings roster file is applied after its node file and replaces the
///      bindings list, so appending while the node fills would be lost.
bool appendCharPropBindings(io::SchemaObject& so, const std::vector<HkyParse::CharPropBind>& mine, HkyParse& ps) {
    if (mine.empty()) return true;
    io::FieldValue& sv = so.FieldRef("variableBindingSet");
    if (!sv.obj) {
        auto s = makeObj(ps.reg, "hkbVariableBindingSet");
        if (!s) { ps.err = ps.where + ": no hkbVariableBindingSet class"; return false; }
        s->SetInt("indexOfBindingToEnable", -1);
        ps.all.push_back(s);
        sv.obj = s;
    }
    auto* set = dynamic_cast<io::SchemaObject*>(sv.obj.get());
    if (!set) { ps.err = ps.where + ": variableBindingSet is not a schema object"; return false; }
    auto& binds = set->FieldRef("bindings").objs;
    for (const auto& b : mine) {
        auto bo = makeObj(ps.reg, "hkbVariableBindingSetBinding");
        if (!bo) { ps.err = ps.where + ": no hkbVariableBindingSetBinding class"; return false; }
        bo->Str("memberPath") = b.path;
        bo->SetInt("bitIndex", -1);
        bo->SetInt("bindingType", 1);
        ps.all.push_back(bo);
        const auto same = std::find_if(binds.begin(), binds.end(), [&](const auto& o) {
            const auto* e = dynamic_cast<const io::SchemaObject*>(o.get());
            return e && e->Str("memberPath") == b.path;
        });
        if (same != binds.end()) *same = bo;
        else binds.push_back(bo);
        ps.symbols.push_back({ bo.get(), *bo->IndexOf("variableIndex"), false, b.name, b.where });
    }
    return true;
}

/// @brief Every pending `charprop` binding, per bindable in the order bindables were first met.
bool appendCharPropBindings(HkyParse& ps) {
    std::vector<io::SchemaObject*> owners;
    std::map<io::SchemaObject*, std::vector<HkyParse::CharPropBind>> by;
    for (const auto& b : ps.charPropBinds) {
        if (!by.count(b.owner)) owners.push_back(b.owner);
        by[b.owner].push_back(b);
    }
    ps.charPropBinds.clear();
    for (io::SchemaObject* o : owners)
        if (!appendCharPropBindings(*o, by[o], ps)) return false;
    return true;
}

bool fillHky(io::SchemaObject& so, const yml::ConstNodeRef& map, HkyParse& ps) {
    if (so.Schema()->vocab == "graph" &&
        (yhas(map, "variables") || yhas(map, "events") || yhas(map, "characterProperties") || yhas(map, "attributes")))
        return fillVocab(so, map, ps);
    if (so.Schema()->vocab == "character" && yhas(map, kCharacterProperties.key)) {
        if (!fillFields(so, map, ps)) return false;
        const yml::ConstNodeRef props = yget(map, kCharacterProperties.key);
        return fillValued(so, kCharacterProperties, &props, ps);
    }
    return fillFields(so, map, ps);
}

/// @brief fillHky's field walk: every schema field the map names (a patch touches only those; a full fill defaults
///        the rest).
bool fillFields(io::SchemaObject& so, const yml::ConstNodeRef& map, HkyParse& ps) {
    const auto fields = so.Fields();
    struct OwnerScope {
        HkyParse& ps; io::SchemaObject* was;
        ~OwnerScope() { ps.boneOwner = was; }
    } scope{ ps, ps.boneOwner };
    for (const Field* f : fields)
        if (f->kind == FieldKind::Ptr && f->ref == "hkaSkeleton") { ps.boneOwner = &so; break; }
    struct BindableScope {
        HkyParse& ps; io::SchemaObject* was; std::string member;
        ~BindableScope() { ps.bindable = was; ps.member = member; }
    } bscope{ ps, ps.bindable, ps.member };
    const bool bindable = so.IndexOf("variableBindingSet").has_value();
    if (bindable) { ps.bindable = &so; ps.member.clear(); }
    for (std::size_t i = 0; i < fields.size(); ++i) {
        const Field& f = *fields[i];
        if (f.name.empty() || (f.ignored && !f.hkyEmit)) continue;
        if (!yhas(map, f.name) && ps.patch) continue;   // a patch touches only what it names
        if (const auto node = yget(map, f.name); !f.charProp.empty() && yhas(map, f.name) && node.is_map() &&
                                                 node.num_children() == 1 && yhas(node, "charprop")) {
            if (!charPropValue(so, i, ystr(yget(node, "charprop")), ps)) return false;
            continue;
        }
        if (!yhas(map, f.name)) {
            if (f.kind == FieldKind::Scalar && f.boneRef) so.FieldAt(i).raw = encInt(-1, f.scalar);
            else if (f.kind == FieldKind::Struct && !so.FieldAt(i).obj) {
                auto d = makeObj(ps.reg, f.ref);
                if (!d) { ps.err = ps.where + ": " + f.name + ": unknown struct class '" + f.ref + "'"; return false; }
                ps.all.push_back(d);
                so.FieldAt(i).obj = d;
            }
            continue;
        }
        bool handled = false;
        if (ps.patch && !patchField(so, i, yget(map, f.name), ps, handled)) return false;
        if (!handled && !fieldValue(so, i, yget(map, f.name), ps)) return false;
        if (!f.roster.empty() && !yget(map, f.name).is_map()) ps.subKeys.erase({ &so, i });
        if (!f.eventRef.empty() && yhas(map, f.eventRef)) ps.symbols.push_back({ &so, i, true,  ystr(yget(map, f.eventRef)), ps.where });
        if (!f.varRef.empty()   && yhas(map, f.varRef))   ps.symbols.push_back({ &so, i, false, ystr(yget(map, f.varRef)),   ps.where });
    }
    return true;
}
} // namespace

std::shared_ptr<io::SchemaObject> ParseEmbeddedObject(const yml::ConstNodeRef& map, const std::string& cls,
                                                      const schema::SchemaRegistry& reg, const std::string& where,
                                                      std::string& err) {
    auto obj = makeObj(reg, cls);
    if (!obj) { err = where + ": unknown class '" + cls + "'"; return nullptr; }
    if (!map.readable() || !map.is_map()) { err = where + ": not a map"; return nullptr; }
    HkyParse ps{ reg, nullptr, {}, {}, {}, {}, {} };
    ps.where = where;
    if (!fillFields(*obj, map, ps)) { err = ps.err; return nullptr; }
    if (!ps.symbols.empty() || !ps.boneNames.empty()) {
        err = where + ": " + cls + " names an event, variable or bone, which an embedded object cannot resolve";
        return nullptr;
    }
    return obj;
}

std::string EmitEmbeddedObject(const io::SchemaObject& so, const schema::SchemaRegistry& reg) {
    const Identity                                       id;
    const NameResolver                                   nr;
    const std::unordered_map<const IHavokObject*, std::string> cat;
    const ReflectCtx                                     c{ id, nr, reg, nullptr, &cat };
    std::string y;
    for (std::size_t i = 0; i < so.Fields().size(); ++i) y += renderField(so, i, c);
    return y;
}

bool ParseHkyReflective(const std::string& dir, const schema::SchemaRegistry& reg, ParsedHky& out, std::string& err,
                        const std::vector<std::string>* bones, const std::string& unitPath) {
    return ParseHkyReflective(CB::engine::common::DiskUnitSource(dir), reg, out, err, bones, unitPath);
}

namespace {
/// @brief One YAML file of a unit. A ROSTER file (`<cat>/<id>/<field.path>.yaml`) carries one roster of node `owner`
///        at `field` (the root's rosters stay inline in graph.yaml); anything else is a node file — `<cat>/<id>.yaml`,
///        or `<cat>/<id>/<id>.yaml` for a node with sub-records.
/// A BONE file (`<skeleton id>/bones/<bone>.yaml`) is one bone of a role skeleton: `owner` the skeleton, `field` the bone.
struct HkyDoc { std::string text; yml::Tree tree; std::string cat; std::string file; bool roster = false, bone = false; std::string owner, field; };
/// @brief Every YAML file of a unit, parsed in place (the text buffer lives beside its tree).
std::vector<std::unique_ptr<HkyDoc>> loadHkyDocs(const CB::engine::common::IUnitSource& src) {
    std::vector<std::unique_ptr<HkyDoc>> docs;
    for (const std::string& rel : src.listYaml("", true)) {
        auto text = src.read(rel);
        if (!text) continue;
        auto d = std::make_unique<HkyDoc>();
        d->text = std::move(*text);
        d->file = rel;
        const auto slash = rel.rfind('/'), first = rel.find('/');
        d->cat = slash == std::string::npos ? std::string() : rel.substr(0, slash);
        if (first != std::string::npos && slash != first && rel.compare(first, slash - first + 1, "/bones/") == 0 &&
            rel.size() > 5 && rel.compare(rel.size() - 5, 5, ".yaml") == 0) {
            d->bone  = true;
            d->owner = rel.substr(0, first);
            d->field = rel.substr(slash + 1, rel.size() - slash - 6);
        } else if (slash != std::string::npos) {
            std::string stem = rel.substr(slash + 1);
            if (stem.size() > 5 && stem.compare(stem.size() - 5, 5, ".yaml") == 0) stem.resize(stem.size() - 5);
            if (first != slash) {
                const std::string folder = d->cat.substr(d->cat.rfind('/') + 1);
                if (stem == folder) {
                    d->cat = d->cat.substr(0, d->cat.rfind('/'));   // `<cat>/<id>/<id>.yaml`: the node's own body
                } else {
                    d->roster = true;
                    d->owner = folder;
                    for (char& ch : d->owner) if (ch == '-') ch = ':';
                    d->field = stem;
                }
            }
        }
        d->tree = yml::parse_in_place(c4::to_csubstr(d->file), c4::to_substr(d->text));
        docs.push_back(std::move(d));
    }
    return docs;
}

/// @brief Apply a node-folder roster file to `owner`: walk the dotted field path through the owner's objects
///        (creating an absent one of the declared class), then fill the roster field whole, or as a patch.
bool applyRosterFile(io::SchemaObject& owner, const HkyDoc& d, bool patch, HkyParse& ps) {
    const yml::ConstNodeRef root = d.tree.crootref();
    io::SchemaObject* obj = &owner;
    ps.where = d.file;
    /// @note The roster's entries continue the member paths of the bindable the field path ends in, as they would have
    ///       inside the node's own file.
    struct BindableScope {
        HkyParse& ps; io::SchemaObject* was; std::string member;
        ~BindableScope() { ps.bindable = was; ps.member = member; }
    } bscope{ ps, ps.bindable, ps.member };
    ps.bindable = owner.IndexOf("variableBindingSet") ? &owner : nullptr;
    ps.member.clear();
    for (std::size_t s = 0;;) {
        const std::size_t dot = d.field.find('.', s);
        const std::string name = d.field.substr(s, dot == std::string::npos ? std::string::npos : dot - s);
        const auto fs = obj->Fields();
        std::size_t j = fs.size();
        for (std::size_t k = 0; k < fs.size(); ++k) if (fs[k]->name == name) { j = k; break; }
        if (j == fs.size()) { ps.err = d.file + ": " + obj->ClassName() + " has no field '" + name + "'"; return false; }
        const Field& f = *fs[j];
        if (dot == std::string::npos) {
            if (f.roster.empty()) { ps.err = d.file + ": " + f.name + " is not a roster"; return false; }
            const bool was = ps.patch;
            ps.patch = patch;
            bool handled = false, ok = true;
            if (patch) ok = patchField(*obj, j, root, ps, handled);
            if (ok && !handled) ok = fieldValue(*obj, j, root, ps);
            ps.patch = was;
            return ok;
        }
        if (f.kind != FieldKind::Ptr && f.kind != FieldKind::Struct) { ps.err = d.file + ": " + f.name + " is not an inline object"; return false; }
        io::FieldValue& v = obj->FieldAt(j);
        if (!v.obj) {
            auto o = makeObj(ps.reg, f.ref);
            if (!o) { ps.err = d.file + ": unknown class '" + f.ref + "'"; return false; }
            ps.all.push_back(o);
            v.obj = o;
        }
        /// @note The path names a LIST, not an object: when an earlier mod re-pointed the field at a node (bfco shares one
        ///       notify-event array between states), the roster applies to the list the owner uses now.
        auto* next = dynamic_cast<io::SchemaObject*>(v.obj.get());
        if (!next) { ps.err = d.file + ": " + f.name + " is not an object"; return false; }
        if (f.kind == FieldKind::Struct) ps.member += f.name + "/";
        else { ps.bindable = next->IndexOf("variableBindingSet") ? next : nullptr; ps.member.clear(); }
        obj = next;
        s = dot + 1;
    }
}

/// @brief A role skeleton's bone order from the bones it has (`present`, its bone files): the boneorder shim's order for
///        the unit and role, then `extra` (its skeleton.yaml `extraBones`: the bones the shim doesn't list, in order).
/// @return nullopt with `err` set when `extra` names a bone without a file, one the shim already orders, or one twice,
///         or a present bone has no place in the order.
std::optional<std::vector<std::string>> skeletonBoneOrder(const schema::SchemaRegistry& reg, const std::string& unitPath,
                                                          const std::string& role, const std::set<std::string>& present,
                                                          const std::vector<std::string>& extra, std::string& err) {
    const std::vector<std::string>* known = nullptr;
    if (const skeleton::BoneOrderEntry* e = skeleton::FindBoneOrder(reg, unitPath))
        if (const auto it = e->roles.find(role); it != e->roles.end()) known = &it->second;
    std::vector<std::string> order;
    if (known) for (const auto& b : *known) if (present.count(b)) order.push_back(b);
    const std::set<std::string> listed = known ? std::set<std::string>(known->begin(), known->end()) : std::set<std::string>{};
    std::set<std::string> placed;
    for (const auto& b : extra) {
        if (!present.count(b)) { err = role + "/skeleton.yaml: extraBones names '" + b + "', which has no bone file"; return std::nullopt; }
        if (listed.count(b)) { err = role + "/skeleton.yaml: extraBones names '" + b + "', which the boneorder shim already orders"; return std::nullopt; }
        if (!placed.insert(b).second) { err = role + "/skeleton.yaml: extraBones names '" + b + "' twice"; return std::nullopt; }
        order.push_back(b);
    }
    if (order.size() != present.size())
        for (const auto& b : present)
            if (std::find(order.begin(), order.end(), b) == order.end()) {
                err = role + "/bones/" + b + ".yaml: the bone has no place in the order (neither the boneorder shim nor extraBones lists it)";
                return std::nullopt;
            }
    return order;
}

/// @brief Build a role skeleton's three bone arrays from its bone files: order = the boneorder shim's for the unit and
///        role, then `extraBones` (the bones it doesn't list, in their order); each bone's parent by name; a ragdoll's
///        `rigidBody` entries rebuilt into the instance's boneToRigidBodyMap.
/// @return false with `ps.err` set: a bone with no place in the order, a listed bone without a file, an unknown parent
///         or body.
bool buildSkeletonBones(io::SchemaObject& skel, const std::string& role, const std::vector<const HkyDoc*>& files,
                        const std::vector<std::string>& extra, const std::string& unitPath, HkyParse& ps) {
    std::map<std::string, const HkyDoc*> byName;
    for (const HkyDoc* d : files) byName[d->field] = d;
    std::set<std::string> present;
    for (const auto& [name, d] : byName) present.insert(name);
    const auto ordered = skeletonBoneOrder(ps.reg, unitPath, role, present, extra, ps.err);
    if (!ordered) return false;
    const std::vector<std::string>& order = *ordered;
    std::map<std::string, std::int16_t> index;
    for (std::size_t k = 0; k < order.size(); ++k) index[order[k]] = static_cast<std::int16_t>(k);
    std::vector<std::int16_t>                     parents;
    std::vector<std::uint8_t>                     pose;
    std::vector<std::shared_ptr<IHavokObject>>    boneObjs;
    std::vector<std::optional<std::string>>       bodies;
    bool                                          folded = false;
    for (const auto& name : order) {
        const HkyDoc& d = *byName.at(name);
        const auto r = d.tree.crootref();
        ps.where = d.file;
        const std::string parent = yhas(r, "parent") ? ystr(yget(r, "parent")) : std::string("null");
        if (parent == "null" && !(yhas(r, "parent") && yget(r, "parent").is_val_quoted())) parents.push_back(-1);
        else if (const auto it = index.find(parent); it != index.end()) parents.push_back(it->second);
        else { ps.err = d.file + ": parent '" + parent + "' is not a bone of this skeleton"; return false; }
        auto bone = makeObj(ps.reg, "hkaBone");
        if (!bone) { ps.err = "hkaBone is not in the schema"; return false; }
        bone->Str("name") = name;
        bone->SetBool("lockTranslation", yhas(r, "lockTranslation") && ystr(yget(r, "lockTranslation")) == "true");
        ps.all.push_back(bone);
        boneObjs.push_back(bone);
        if (!yhas(r, "pose")) { ps.err = d.file + ": no pose"; return false; }
        const auto p = yget(r, "pose");
        for (const char* k : { "translation", "rotation", "scale" }) {
            if (!yhas(p, k)) { ps.err = d.file + ": pose has no " + k; return false; }
            const auto b = vec4Strict(ystr(yget(p, k)));
            pose.insert(pose.end(), b.begin(), b.end());
        }
        if (yhas(r, "rigidBody")) { folded = true; bodies.push_back(ystr(yget(r, "rigidBody"))); }
        else bodies.push_back(std::nullopt);
    }
    skel.SetArray("parentIndices", parents);
    skel.FieldRef("bones").objs = std::move(boneObjs);
    skel.FieldRef("referencePose").raw = std::move(pose);
    if (!folded) return true;
    io::SchemaObject* instance = nullptr;
    for (const auto& o : ps.all)
        if (std::string(o->ClassName()) == "hkaRagdollInstance" && o->Child("skeleton") == &skel) { instance = o.get(); break; }
    if (!instance) { ps.err = role + ": its bones name rigid bodies, but no ragdoll instance uses this skeleton"; return false; }
    const auto& rigid = instance->Children("rigidBodies");
    std::vector<std::int32_t> map;
    for (std::size_t k = 0; k < order.size(); ++k) {
        const std::string id = bodies[k].value_or("null");
        if (id == "null") { map.push_back(-1); continue; }
        const auto it = ps.byId.find(ps.canon ? ps.canon(id) : id);
        const auto at = it == ps.byId.end() ? rigid.end() : std::find(rigid.begin(), rigid.end(), it->second);
        if (at == rigid.end()) { ps.err = role + "/bones/" + order[k] + ".yaml: rigidBody '" + id + "' is not a body of the ragdoll instance"; return false; }
        map.push_back(static_cast<std::int32_t>(at - rigid.begin()));
    }
    instance->SetArray("boneToRigidBodyMap", map);
    return true;
}
} // namespace

bool ParseHkyReflective(const CB::engine::common::IUnitSource& src, const schema::SchemaRegistry& reg, ParsedHky& out,
                        std::string& err, const std::vector<std::string>* bones, const std::string& unitPath) {
    HkyParse ps{ reg, bones, {}, {}, {}, {}, {} };
    auto docs = loadHkyDocs(src);
    // Pass 1: create every file node (and the root) so ids resolve regardless of file order.
    std::shared_ptr<io::SchemaObject> root;
    for (auto& d : docs) {
        if (d->roster || d->bone) continue;
        const auto r = d->tree.crootref();
        const std::string id = ystr(yget(r, "id")), cls = ystr(yget(r, "class"));
        auto obj = makeObj(reg, cls);
        if (!obj) { err = d->file + ": unknown class '" + cls + "'"; return false; }
        if (ps.byId.count(id)) { err = d->file + ": duplicate id '" + id + "'"; return false; }
        ps.byId[id] = obj;
        ps.all.push_back(obj);
        out.identity.ids[obj.get()] = id;
        out.identity.category[obj.get()] = d->cat;
        if (cls == "hkRootLevelContainer") root = obj;
    }
    if (!root) { err = "no graph.yaml (hkRootLevelContainer)"; return false; }
    // Pass 2: fill the nodes, then each node-folder roster into its owner.
    std::map<std::string, std::vector<std::string>> extraBones;   ///< role skeleton id -> its skeleton.yaml `extraBones`
    for (auto& d : docs) {
        if (d->roster || d->bone) continue;
        ps.where = d->file;
        const auto r = d->tree.crootref();
        const std::string id = ystr(yget(r, "id"));
        if (!fillHky(*ps.byId.at(id), r, ps)) { err = ps.err; return false; }
        if (yhas(r, "extraBones")) for (auto e : yget(r, "extraBones")) extraBones[id].push_back(ystr(e));
    }
    for (auto& d : docs) {
        if (!d->roster) continue;
        const auto it = ps.byId.find(d->owner);
        io::SchemaObject* owner = it == ps.byId.end() ? nullptr : it->second.get();
        if (!owner) { err = d->file + ": no node '" + d->owner + "' for this roster"; return false; }
        if (!applyRosterFile(*owner, *d, false, ps)) { err = ps.err; return false; }
    }
    // Pass 3: the role skeletons' bones (from their bone files), then the `boneof` names that index them.
    {
        std::map<std::string, std::vector<const HkyDoc*>> boneFiles;
        for (const auto& d : docs) if (d->bone) boneFiles[d->owner].push_back(d.get());
        for (const auto& [owner, files] : boneFiles) {
            const auto it = ps.byId.find(owner);
            if (it == ps.byId.end() || std::string(it->second->ClassName()) != "hkaSkeleton") {
                err = owner + "/bones/: no skeleton '" + owner + "' for these bone files";
                return false;
            }
            if (!buildSkeletonBones(*it->second, owner, files, extraBones[owner], unitPath, ps)) { err = ps.err; return false; }
        }
        for (const auto& b : ps.boneNames) {
            const io::SchemaObject* skel = b.owner ? b.owner->Child(b.skeletonField) : nullptr;
            const auto names = skel ? boneNamesOf(*skel) : std::vector<std::string>{};
            const auto at = std::find(names.begin(), names.end(), b.name);
            if (at == names.end()) { err = b.where + ": bone '" + b.name + "' is not in the " + b.skeletonField + " skeleton"; return false; }
            const Field& f = *b.obj->Fields()[b.field];
            const auto enc = encInt(static_cast<std::int64_t>(at - names.begin()), f.scalar);
            auto& raw = b.obj->FieldAt(b.field).raw;
            std::copy(enc.begin(), enc.end(), raw.begin() + static_cast<std::ptrdiff_t>(b.elem * enc.size()));
        }
    }
    if (!appendCharPropBindings(ps)) { err = ps.err; return false; }
    // Every inline object joins the identity (category "") so the name resolver and re-emit see the whole graph.
    int anon = 0;
    for (const auto& o : ps.all)
        if (!out.identity.category.count(o.get())) { out.identity.category[o.get()] = ""; out.identity.ids[o.get()] = "~" + std::to_string(++anon); }
    // Name companions: the NAME wins over the index (indices shift when mods add symbols; names don't).
    const NameResolver nr = buildResolver(out.identity);
    for (const auto& s : ps.symbols) {
        const auto& L = s.isEvent ? nr.eventsL
                      : condTrue(*s.obj, s.obj->Fields()[s.field]->charPropIf) ? nr.charPropsL : nr.variablesL;
        const auto idx = L.encode(s.name);
        if (!idx) { err = s.where + ": " + (s.isEvent ? "event" : "variable") + " '" + s.name + "' is not in the graph's roster"; return false; }
        const Field& f = *s.obj->Fields()[s.field];
        s.obj->FieldAt(s.field).raw = encInt(static_cast<std::int64_t>(*idx), f.scalar);
    }
    out.root = root;
    out.objects = std::move(ps.all);
    out.identity.subKeys = std::move(ps.subKeys);
    return true;
}

// ── Field patches — reflective .hky DELTAS: derive (base vs mod -> patch) and merge (base + patch -> graph) ────────
namespace {

std::string formIdOf(std::uint16_t idx, const std::string& decId) {
    char* end = nullptr;
    const unsigned long v = std::strtoul(decId.c_str(), &end, 10);
    if (end && *end == '\0')
        if (const auto f = CB::engine::codec::formid::make(idx, static_cast<std::uint32_t>(v)))
            return CB::engine::codec::formid::format(*f);
    return decId;
}

bool isArrayKind(FieldKind k) {
    return k == FieldKind::PtrArray || k == FieldKind::StructArray || k == FieldKind::StringArray ||
           k == FieldKind::ScalarArray || k == FieldKind::Vec4Array || k == FieldKind::QsTransformArray || k == FieldKind::BoolArray;
}

/// @brief A `boneweights` array's PER-BONE patch: `'Bone': weight` for each bone whose weight changed (indent 2; empty
///        = unchanged). nullopt when the arrays can't be compared bone by bone (no rig, or the length changed).
std::optional<std::string> boneWeightsDiff(const Field& f, const io::FieldValue& vb, const io::FieldValue& vm, const ReflectCtx& c) {
    if (!f.boneWeights || f.scalar != Scalar::Float || !c.bones) return std::nullopt;
    const std::size_t n = vb.raw.size() / 4;
    if (vm.raw.size() != vb.raw.size() || n > c.bones->size()) return std::nullopt;
    std::string y;
    for (std::size_t k = 0; k < n; ++k) {
        const std::string wb = rfloat(slice(vb.raw, k, 4)), wm = rfloat(slice(vm.raw, k, 4));
        if (wb != wm) y += "  " + text::Quote((*c.bones)[k]) + ": " + wm + "\n";
    }
    return y;
}

/// @brief A `boneset` list's patch: `'Bone': {}` for each bone added, `'Bone': {deleted: true}` for each removed (indent
///        2; empty = same set). nullopt when either side isn't a set of rig bones (an unknown index, a duplicate).
std::optional<std::string> boneSetDiff(const Field& f, const io::FieldValue& vb, const io::FieldValue& vm, const ReflectCtx& c) {
    if (!f.boneSet || !c.bones) return std::nullopt;
    const std::size_t w = static_cast<std::size_t>(schema::ScalarWidth(f.scalar));
    auto bonesOf = [&](const io::FieldValue& v) -> std::optional<std::vector<std::int64_t>> {
        std::vector<std::int64_t> out;
        for (std::size_t k = 0; w && k < v.raw.size() / w; ++k) {
            const std::int64_t bi = decodeInt(slice(v.raw, k, w), f.scalar);
            if (bi < 0 || bi >= static_cast<std::int64_t>(c.bones->size()) || std::find(out.begin(), out.end(), bi) != out.end()) return std::nullopt;
            out.push_back(bi);
        }
        return out;
    };
    const auto b = bonesOf(vb), m = bonesOf(vm);
    if (!b || !m) return std::nullopt;
    std::string y;
    for (const std::int64_t bi : *m)
        if (std::find(b->begin(), b->end(), bi) == b->end()) y += "  " + text::Quote((*c.bones)[static_cast<std::size_t>(bi)]) + ": {}\n";
    for (const std::int64_t bi : *b)
        if (std::find(m->begin(), m->end(), bi) == m->end()) y += "  " + text::Quote((*c.bones)[static_cast<std::size_t>(bi)]) + ": {deleted: true}\n";
    return y;
}

using FieldSkip = std::function<bool(const std::string&)>;
std::string diffObject(const io::SchemaObject& b, const io::SchemaObject& m, const ReflectCtx& cb, const ReflectCtx& cm,
                       const FieldSkip& skip = {});

/// @brief A changed POINTER vocabulary entry (a character bone-weight preset) as a PATCH record: type / role / flags in
///        full, and `value:` as only what changed inside the preset (its changed bones), when both sides hold the same
///        inline class. nullopt otherwise (the caller writes the whole record).
std::optional<std::string> pointerRecordPatch(const io::SchemaObject& b, const io::SchemaObject& m, const ValuedRoster& r,
                                              std::size_t bpos, std::size_t mpos, const ReflectCtx& cb, const ReflectCtx& cm) {
    const auto bp = valuedPointer(b, r, bpos), mp = valuedPointer(m, r, mpos);
    const auto* bo = dynamic_cast<const io::SchemaObject*>(bp.get());
    const auto* mo = dynamic_cast<const io::SchemaObject*>(mp.get());
    const auto* info = valuedInfo(m, r, mpos);
    if (!bo || !mo || !info || isFileNode(bp.get(), cb) || isFileNode(mp.get(), cm) || std::string(bo->ClassName()) != mo->ClassName())
        return std::nullopt;
    std::string y = "type: " + enumText("VariableType", static_cast<long>(info->GetInt("type")), false) + "\n";
    if (const auto* role = info->Child("role")) {
        y += "role: " + enumText("Role", static_cast<long>(role->GetInt("role")), false) + "\n";
        y += "flags: " + enumText("RoleFlags", static_cast<long>(static_cast<std::uint16_t>(role->GetInt("flags"))), true) + "\n";
    }
    const std::string sub = diffObject(*bo, *mo, inlCtx(cb), inlCtx(cm));
    return sub.empty() ? y : y + "value:\n" + indentLines(sub, "  ", "  ");
}

/// @brief A roster field's KEYED patch: changed entries (field-level for an inline record), new entries (whole, keyed
///        as the mod's own), and `{deleted: true}` for base entries the mod dropped. Returns the entry lines (indented
///        two), empty when nothing changed; nullopt when either side can't be keyed (the caller patches positionally).
/// @note  Pairing a mod's entry with vanilla's happens ONCE, here at convert time: `ref`/`field`/`bone` pair by key;
///        minted `id` pairs identical records first, then the remaining ones in order (an edited record overrides its
///        original); a `slot` pairs by position and can't shrink.
std::optional<std::string> rosterDiff(const io::SchemaObject& b, const io::SchemaObject& m, std::size_t i,
                                      const ReflectCtx& cb, const ReflectCtx& cm) {
    const Field& f = *m.Fields()[i];
    if (!rosterEntries(b, i, cb) || !rosterEntries(m, i, cm)) return std::nullopt;
    const auto [kind, keyField] = rosterKind(f);
    const auto& B = b.Values()[i].objs;
    const auto& M = m.Values()[i].objs;
    if (kind == "slot" && M.size() < B.size()) return std::nullopt;
    const ReflectCtx bi = inlCtx(cb), mi = inlCtx(cm);
    auto body = [&](const std::shared_ptr<IHavokObject>& p, const ReflectCtx& c0) -> std::string {
        const ReflectCtx c = inlCtx(c0);
        if (!p) return "null";
        if (isFileNode(p.get(), c)) return refIdOf(p, c.id);
        const auto* e = dynamic_cast<const io::SchemaObject*>(p.get());
        return e ? std::string(e->ClassName()) + "\n" + reflectObject(*e, c, f.ref) : std::string("?");
    };
    auto natural = [&](const std::shared_ptr<IHavokObject>& p, const ReflectCtx& c) {
        const auto* e = dynamic_cast<const io::SchemaObject*>(p.get());
        return e ? naturalKey(*e, kind, keyField, c).first : std::string();
    };
    std::vector<std::string> bb, mb;
    for (const auto& p : B) bb.push_back(body(p, cb));
    for (const auto& p : M) mb.push_back(body(p, cm));
    std::vector<long> pairOf(M.size(), -1);
    std::vector<bool> claimed(B.size(), false);
    auto link = [&](std::size_t mk, std::size_t bk) { pairOf[mk] = static_cast<long>(bk); claimed[bk] = true; };
    if (kind == "slot") {
        for (std::size_t k = 0; k < B.size(); ++k) link(k, k);
    } else if (kind == "field" || kind == "bone") {
        for (std::size_t mk = 0; mk < M.size(); ++mk)
            for (std::size_t bk = 0; bk < B.size(); ++bk)
                if (!claimed[bk] && natural(B[bk], cb) == natural(M[mk], cm)) { link(mk, bk); break; }
    } else {
        for (std::size_t mk = 0; mk < M.size(); ++mk)
            for (std::size_t bk = 0; bk < B.size(); ++bk)
                if (!claimed[bk] && bb[bk] == mb[mk]) { link(mk, bk); break; }
        if (kind == "id") {
            std::vector<std::size_t> ub, um;
            for (std::size_t bk = 0; bk < B.size(); ++bk) if (!claimed[bk]) ub.push_back(bk);
            for (std::size_t mk = 0; mk < M.size(); ++mk) if (pairOf[mk] < 0) um.push_back(mk);
            for (std::size_t t = 0; t < ub.size() && t < um.size(); ++t)
                if (bb[ub[t]].substr(0, bb[ub[t]].find('\n')) == mb[um[t]].substr(0, mb[um[t]].find('\n'))) link(um[t], ub[t]);
        }
    }
    auto baseKey = [&](std::size_t bk) {
        if (kind == "ref") return refIdOf(B[bk], cb.id);
        if (kind == "field" || kind == "bone") return natural(B[bk], cb);
        return mintedKey(bk, cb.keyOwner);
    };
    auto whole = [&](const std::shared_ptr<IHavokObject>& p) -> std::string {
        if (!p) return "null";
        if (isFileNode(p.get(), cm)) return refIdOf(p, cm.id);
        const auto* e = dynamic_cast<const io::SchemaObject*>(p.get());
        return e ? reflectObject(*e, mi, f.ref) : std::string();
    };
    std::string y;
    std::size_t fresh = 0;
    for (std::size_t mk = 0; mk < M.size(); ++mk) {
        if (pairOf[mk] >= 0) {
            const std::size_t bk = static_cast<std::size_t>(pairOf[mk]);
            if (bb[bk] == mb[mk]) continue;
            const auto* bo = dynamic_cast<const io::SchemaObject*>(B[bk].get());
            const auto* mo = dynamic_cast<const io::SchemaObject*>(M[mk].get());
            if (bo && mo && !isFileNode(B[bk].get(), cb) && !isFileNode(M[mk].get(), cm) && std::string(bo->ClassName()) == mo->ClassName()) {
                const std::string sub = diffObject(*bo, *mo, bi, mi);
                if (!sub.empty()) y += rosterEntry(baseKey(bk), sub);
            } else {
                y += rosterEntry(baseKey(bk), whole(M[mk]));
            }
            continue;
        }
        std::string key;
        if (kind == "ref") key = refIdOf(M[mk], cm.id);
        else if (kind == "field" || kind == "bone") key = natural(M[mk], cm);
        else key = mintedKey(fresh++, cm.keyOwner);
        y += rosterEntry(key, whole(M[mk]));
    }
    for (std::size_t bk = 0; bk < B.size(); ++bk)
        if (!claimed[bk]) y += text::Quote(baseKey(bk)) + ": {deleted: true}\n";
    return y;
}

/// @brief The field-level patch turning `b` into `m` (empty = identical). Nested inline objects of the same class
///        deep-diff; compose/union arrays whose base is a prefix emit only the appended tail (else `{replace: [...]}`);
///        a value that became absent is written explicitly (`null` / `[]` / -1) — a patch never relies on omission.
std::string diffObject(const io::SchemaObject& b, const io::SchemaObject& m, const ReflectCtx& cb, const ReflectCtx& cm,
                       const FieldSkip& skip) {
    std::string y;
    if (!skip && !m.Schema()->vocab.empty() && vocabRenderable(b) && vocabRenderable(m, true) &&
        !isFileNode(b.Child("stringData"), cb) && !isFileNode(m.Child("stringData"), cm)) {
        /// @note A name the mod duplicated (a rename onto an existing name) takes the occurrence at the BASE entry's
        ///       position — the one it didn't rename; a name new to the roster takes its first occurrence.
        using Key = std::pair<std::string, std::string>;
        std::map<Key, std::pair<std::size_t, std::string>> base;
        std::map<std::string, std::size_t> pos;
        for (const auto& [k, name, rec] : vocabRecords(b, inlCtx(cb), true)) base[{ k, name }] = { pos[k]++, rec };
        std::map<Key, std::pair<std::size_t, std::string>> mod;
        pos.clear();
        std::vector<Key> order;
        for (const auto& [k, name, rec] : vocabRecords(m, inlCtx(cm), true)) {
            const std::size_t at = pos[k]++;
            const auto bi = base.find({ k, name });
            auto [it, fresh] = mod.try_emplace({ k, name }, at, rec);
            if (fresh) order.push_back({ k, name });
            else if (bi != base.end() && at == bi->second.first) it->second = { at, rec };
        }
        std::map<std::string, std::string> blocks;
        const ValuedRoster& valued = m.Schema()->vocab == "character" ? kCharacterProperties : kGraphVariables;
        for (const Key& key : order) {
            const auto& [mpos, rec] = mod.at(key);
            const auto it = base.find(key);
            if (it != base.end() && it->second.second == rec) continue;
            std::string out = rec;
            if (it != base.end() && key.first == valued.key)
                if (const auto p = pointerRecordPatch(b, m, valued, it->second.first, mpos, cb, cm)) out = *p;
            blocks[key.first] += vocabLine(key.second, out);
        }
        for (const char* k : vocabKeys(m))
            if (!blocks[k].empty()) y += std::string(k) + ":\n" + blocks[k];
        if (m.Schema()->vocab == "graph") return y;
        /// @note A character's other fields diff as usual; its string data diffs without the names the records carry.
        std::string rest = diffObject(b, m, cb, cm, [&](const std::string& n) { return vocabField(m, n) || n == "stringData"; });
        const std::string sd = diffObject(*b.Child("stringData"), *m.Child("stringData"), subCtx(cb, "stringData"),
                                          subCtx(cm, "stringData"), [](const std::string& n) { return n == kCharacterProperties.names; });
        if (!sd.empty()) rest += "stringData:\n" + indentLines(sd, "  ", "  ");
        return rest + y;
    }
    const auto fields = m.Fields();
    for (std::size_t i = 0; i < fields.size(); ++i) {
        const Field& f = *fields[i];
        if (f.name.empty() || (f.ignored && !f.hkyEmit) || (skip && skip(f.name))) continue;
        const std::string tb = renderField(b, i, inlCtx(cb)), tm = renderField(m, i, inlCtx(cm));
        if (tb == tm) continue;
        if (tm.empty()) {
            if (f.kind == FieldKind::Ptr || f.kind == FieldKind::Struct) y += f.name + ": null\n";
            else if (isArrayKind(f.kind))                              y += f.name + ": []\n";
            else if (f.kind == FieldKind::Scalar && f.boneRef)         y += f.name + ": -1\n";
            continue;
        }
        const io::FieldValue& vb = b.Values()[i];
        const io::FieldValue& vm = m.Values()[i];
        if ((f.kind == FieldKind::Ptr || f.kind == FieldKind::Struct) && vb.obj && vm.obj &&
            !isFileNode(vb.obj.get(), cb) && !isFileNode(vm.obj.get(), cm) &&
            std::string(vb.obj->ClassName()) == vm.obj->ClassName() && vm.obj->ClassName() != std::string()) {
            const auto* bo = dynamic_cast<const io::SchemaObject*>(vb.obj.get());
            const auto* mo = dynamic_cast<const io::SchemaObject*>(vm.obj.get());
            const schema::ClassSchema* dcs = cm.reg.Find(f.ref);
            const bool collapsible = dcs && !dcs->unwrap.empty();
            /// @note An empty sub-diff means UNCHANGED even though the texts differed: base-owned roster keys render as
            ///       the master's (`0001x…`), the mod's as its own (`0000x…`). A sub-diff of only the `unwrap:` field is
            ///       written collapsed, like the object itself (a weight map's changed bones right under the field).
            if (bo && mo) {
                const std::string sub = diffObject(*bo, *mo, subCtx(cb, f.name), subCtx(cm, f.name));
                if (!sub.empty()) {
                    const std::string u = collapsible ? unwrapped(*mo, sub, f.name) : std::string();
                    y += u.empty() ? f.name + ":\n" + indentLines(sub, "  ", "  ") : u;
                }
                continue;
            }
        }
        if (f.kind == FieldKind::ScalarArray) {
            if (const auto d = f.boneWeights ? boneWeightsDiff(f, vb, vm, cm) : boneSetDiff(f, vb, vm, cm)) {
                if (!d->empty()) y += f.name + ":\n" + *d;
                continue;
            }
            if (f.boneWeights && f.scalar == Scalar::Float) { y += boneWeightsText(f, vm.raw, inlCtx(cm), true); continue; }
        }
        if ((f.kind == FieldKind::PtrArray || f.kind == FieldKind::StructArray) && !f.roster.empty())
            if (const auto r = rosterDiff(b, m, i, cb, cm)) {
                if (r->empty()) continue;
                if (cm.sink) cm.sink->push_back({ cm.path + f.name, *r });
                else y += f.name + ":\n" + indentLines(*r, "  ", "  ");
                continue;
            }
        /// @note A `union` name roster only ever ADDS the names it lacks (names are identity; a mod never replaces or
        ///       reorders one). Every other list without a roster is a whole value.
        if (f.kind == FieldKind::StringArray && mergeTagOf(cm.reg, m.ClassName(), f.name) == "union") {
            const std::set<std::string> have(vb.strs.begin(), vb.strs.end());
            std::string list;
            for (const auto& s2 : vm.strs) if (!have.count(s2)) list += (list.empty() ? "" : ", ") + text::Quote(s2);
            if (!list.empty()) y += f.name + ": [" + list + "]\n";
            continue;
        }
        y += tm;
    }
    return y;
}
} // namespace

namespace {
/// @brief The derive over a base identity whose ids are already FormIDs of the PATCH's master table; `keyOwner` is
///        the master index of the base's positional roster keys.
bool derivePatch(const Identity& idB, std::uint16_t keyOwner, const std::vector<std::uint8_t>& modBytes,
                 const schema::SchemaRegistry& reg, const std::string& patchDir, std::string& err,
                 const std::vector<std::string>* bones, HkyPatchStats* stats) {
    namespace fs = std::filesystem;
    try {
        PackFileDeserializer desM;
        desM.ObjectFactory = io::MakeSchemaFactory(reg);
        { BinaryReaderEx br(false, true, modBytes); desM.Deserialize(br); }
        Identity idM = AssignIdentity(desM, reg);
        const auto effB = EffectiveCategories(idB), effM = EffectiveCategories(idM);
        const NameResolver nrB = buildResolver(idB), nrM = buildResolver(idM);
        auto isRoot = [](const IHavokObject* o) { return std::string(o->ClassName()) == "hkRootLevelContainer"; };
        auto nameOf = [](const IHavokObject* o) {
            const auto* so = dynamic_cast<const io::SchemaObject*>(o);
            const io::FieldValue* v = so ? so->FindField("name") : nullptr;
            return v ? v->str : std::string();
        };
        const ReflectCtx cB{ idB, nrB, reg, bones, &effB, keyOwner };

        // Base lookups: named nodes by (class, name); unnamed shared leaves by rendered content; the root.
        std::map<std::pair<std::string, std::string>, std::vector<const IHavokObject*>> baseNamed;
        std::map<std::string, std::vector<const IHavokObject*>> baseContent;
        std::unordered_map<std::string, const IHavokObject*> baseById;
        const IHavokObject* baseRoot = nullptr;
        /// @note Vanilla repeats identical unnamed leaves (mt_behavior has three 'AnimObjectPickAxeWall' payloads), so a key
        ///       holds EVERY candidate, sorted by FormID, and the mod's nodes (sorted by their own id) take them in order.
        ///       Both sides number in encounter order, so that pairing is the natural identity — and it no longer depends on
        ///       effB's POINTER-order iteration, which kept a heap-layout-dependent one and made the rest spurious new nodes.
        const auto lowerId = [&](const IHavokObject* a, const IHavokObject* b) {
            const std::string& ia = idB.ids.at(a);
            const std::string& ib = idB.ids.at(b);
            const auto fa = CB::engine::codec::formid::parse(ia), fb = CB::engine::codec::formid::parse(ib);
            if (fa && fb) return std::tie(fa->masterIndex, fa->local) < std::tie(fb->masterIndex, fb->local);
            return ia < ib;
        };
        for (const auto& [o, cat] : effB) {
            if (cat.empty() && !isRoot(o)) continue;
            baseById[idB.ids.at(o)] = o;
            if (isRoot(o)) { baseRoot = o; continue; }
            if (const std::string n = nameOf(o); !n.empty()) baseNamed[{ o->ClassName(), n }].push_back(o);
            else if (const auto* so = dynamic_cast<const io::SchemaObject*>(o))
                baseContent[std::string(o->ClassName()) + "\n" + reflectObject(*so, cB)].push_back(o);
        }
        for (auto& [_, c] : baseNamed)   std::sort(c.begin(), c.end(), lowerId);
        for (auto& [_, c] : baseContent) std::sort(c.begin(), c.end(), lowerId);

        /// @note Identity by (class, name) — unique per vanilla graph (0 dups / 35,661 nodes); a mod duplicate takes the
        ///       base id first-come in read order, the rest are new. Unnamed shared leaves (conditions, payloads) match by
        ///       content. Everything unmatched is a NEW node: FormId(self=0, seq).
        std::vector<const IHavokObject*> modNodes;
        for (const auto& [o, cat] : effM) if (!cat.empty() || isRoot(o)) modNodes.push_back(o);
        std::sort(modNodes.begin(), modNodes.end(), [&](const IHavokObject* a, const IHavokObject* b) {
            return std::strtoul(idM.ids.at(a).c_str(), nullptr, 10) < std::strtoul(idM.ids.at(b).c_str(), nullptr, 10);
        });
        const ReflectCtx cM0{ idM, nrM, reg, bones, &effM };   // content keys only (leaves carry no refs)
        std::set<const IHavokObject*> taken;
        const auto firstFree = [&](const std::vector<const IHavokObject*>& candidates) -> const IHavokObject* {
            for (const auto* b : candidates) if (!taken.count(b)) return b;
            return nullptr;
        };
        std::unordered_map<const IHavokObject*, std::string> modId;
        int seq = 0;
        for (const IHavokObject* o : modNodes) {
            const IHavokObject* match = nullptr;
            if (isRoot(o)) match = baseRoot;
            else if (const std::string n = nameOf(o); !n.empty()) {
                if (auto it = baseNamed.find({ o->ClassName(), n }); it != baseNamed.end()) match = firstFree(it->second);
            } else if (const auto* so = dynamic_cast<const io::SchemaObject*>(o)) {
                if (auto it = baseContent.find(std::string(o->ClassName()) + "\n" + reflectObject(*so, cM0)); it != baseContent.end())
                    match = firstFree(it->second);
            }
            if (match && !taken.count(match)) { taken.insert(match); modId[o] = idB.ids.at(match); }
            else modId[o] = formIdOf(CB::engine::codec::formid::SELF_INDEX, std::to_string(++seq));
        }
        for (const auto& [o, id] : modId) idM.ids[o] = id;
        const ReflectCtx cM{ idM, nrM, reg, bones, &effM };

        std::error_code ec;
        fs::remove_all(patchDir, ec);
        for (const IHavokObject* o : modNodes) {
            const auto* mo = dynamic_cast<const io::SchemaObject*>(o);
            if (!mo) continue;
            const std::string id = idM.ids.at(o);
            const std::string cat = effM.at(o);
            std::string body;
            std::vector<std::pair<std::string, std::string>> rosters;
            ReflectCtx cMn = cM;
            if (!isRoot(o)) cMn.sink = &rosters;
            if (auto it = baseById.find(id); it != baseById.end()) {
                const auto* bo = dynamic_cast<const io::SchemaObject*>(it->second);
                if (!bo || std::string(bo->ClassName()) != mo->ClassName()) { err = "derive: id " + id + " class drift"; return false; }
                body = diffObject(*bo, *mo, cB, cMn);
                if (body.empty() && rosters.empty()) continue;
                if (stats) ++stats->patched;
            } else {
                body = reflectObject(*mo, cMn);
                if (stats) ++stats->added;
            }
            /// @note Same layout as a full unit: a node with sub-records is a folder (body + rosters). A patch states its
            ///       class for the reader (a new node's body already carries it); a node whose only change is a roster
            ///       ships just that roster file in its folder.
            const fs::path dir = isRoot(o) ? fs::path(patchDir) : rosters.empty() ? fs::path(patchDir) / cat
                                                                                  : fs::path(patchDir) / cat / fsId(id);
            if (!body.empty() || !baseById.count(id)) {
                const std::string head = "id: " + id + "\n" + (baseById.count(id) ? "class: " + std::string(mo->ClassName()) + "\n" : std::string());
                if (!disk::WriteFile(dir / (isRoot(o) ? std::string("graph.yaml") : fsId(id) + ".yaml"), head + body, {}, &err))
                    return false;
            }
            for (const auto& [path, entries] : rosters)
                if (!disk::WriteFile(dir / (path + ".yaml"), entries, {}, &err)) return false;
        }
        return true;
    } catch (const std::exception& e) {
        err = std::string("DeriveHkyPatch: ") + e.what();
        return false;
    }
}

/// @brief A unit's id -> canonical owner key: FormId "idx:local" resolves through the unit's master table
///        (`masters[idx]:local`); anything else stays verbatim.
std::function<std::string(const std::string&)> canonFor(const std::vector<std::string>& masters) {
    return [masters](const std::string& id) {
        if (const auto f = CB::engine::codec::formid::parse(id); f && f->masterIndex < masters.size())
            return masters[f->masterIndex] + ":" + std::to_string(f->local);
        return id;
    };
}

/// @brief A canonical owner key ("stem:local") -> the FormID naming it through `masters`; anything else (an inline
///        object's "~N", a stem absent from the table) stays verbatim.
std::string relFormId(const std::string& canon, const std::vector<std::string>& masters) {
    const auto c = canon.rfind(':');
    if (c == std::string::npos) return canon;
    const auto it = std::find(masters.begin(), masters.end(), canon.substr(0, c));
    if (it == masters.end()) return canon;
    return formIdOf(static_cast<std::uint16_t>(it - masters.begin()), canon.substr(c + 1));
}
} // namespace

bool DeriveHkyPatch(const std::vector<std::uint8_t>& baseBytes, const std::vector<std::uint8_t>& modBytes,
                    const schema::SchemaRegistry& reg, const std::string& patchDir, std::string& err,
                    const std::vector<std::string>* bones, std::uint16_t baseIndex, HkyPatchStats* stats) {
    try {
        PackFileDeserializer desB;
        desB.ObjectFactory = io::MakeSchemaFactory(reg);
        { BinaryReaderEx br(false, true, baseBytes); desB.Deserialize(br); }
        Identity idB = AssignIdentity(desB, reg);
        for (auto& [o, id] : idB.ids) id = formIdOf(baseIndex, id);
        return derivePatch(idB, baseIndex, modBytes, reg, patchDir, err, bones, stats);
    } catch (const std::exception& e) {
        err = std::string("DeriveHkyPatch: ") + e.what();
        return false;
    }
}

bool DeriveHkyPatch(const ParsedHky& base, const std::vector<std::string>& baseMasters,
                    const std::vector<std::string>& patchMasters, const std::vector<std::uint8_t>& modBytes,
                    const schema::SchemaRegistry& reg, const std::string& patchDir, std::string& err,
                    const std::vector<std::string>* bones, HkyPatchStats* stats) {
    if (baseMasters.empty()) { err = "DeriveHkyPatch: the base has no master table"; return false; }
    const auto baseCanon = canonFor(baseMasters);
    /// @note Ids a merge added are already canonical ("bfco:54"); the base unit's own FormIds resolve through
    ///       `baseMasters`. Only the graph reachable from the root counts — a replaced subtree's old objects linger
    ///       in the identity, and a stale (class, name) there would steal the live node's match.
    const auto rel = [&](const std::string& id) {
        return relFormId(id.find(':') != std::string::npos ? id : baseCanon(id), patchMasters);
    };
    Identity idB;
    std::unordered_set<const IHavokObject*> seen;
    std::function<void(const IHavokObject*)> walk = [&](const IHavokObject* o) {
        if (!o || !seen.insert(o).second) return;
        if (auto it = base.identity.category.find(o); it != base.identity.category.end()) idB.category[o] = it->second;
        if (auto it = base.identity.ids.find(o); it != base.identity.ids.end()) idB.ids[o] = rel(it->second);
        if (const auto* so = dynamic_cast<const io::SchemaObject*>(o))
            for (const io::FieldValue& fv : so->Values()) { walk(fv.obj.get()); for (const auto& e : fv.objs) walk(e.get()); }
    };
    walk(base.root.get());
    for (const auto& [k, keys] : base.identity.subKeys) {
        if (!seen.count(k.first)) continue;
        auto& out = idB.subKeys[k];
        for (const std::string& key : keys) out.push_back(rel(key));
    }
    const auto self = std::find(patchMasters.begin(), patchMasters.end(), baseMasters.front());
    if (self == patchMasters.end()) { err = "DeriveHkyPatch: the base unit's owner '" + baseMasters.front() + "' is not a master"; return false; }
    return derivePatch(idB, static_cast<std::uint16_t>(self - patchMasters.begin()), modBytes, reg, patchDir, err, bones, stats);
}

bool MergeHkyPatch(ParsedHky& base, const CB::engine::common::IUnitSource& patch, const schema::SchemaRegistry& reg,
                   const std::vector<std::string>& baseMasters, const std::vector<std::string>& patchMasters,
                   std::string& err, const std::vector<std::string>* bones, HkyPatchStats* stats) {
    HkyParse ps{ reg, bones, {}, {}, {}, {}, {} };
    ps.canon = canonFor(patchMasters);
    const auto baseCanon = canonFor(baseMasters);
    ps.baseCanon = baseCanon;
    ps.subKeys = base.identity.subKeys;
    /// @note Hand every object this merge created back to `base` on EVERY exit: a failed patch leaves the graph
    ///       pointing at its new objects, and dropping `ps.all` then left dangling pointers (a crash on the failure dump).
    struct Keep {
        ParsedHky& b; HkyParse& p;
        ~Keep() { if (!p.all.empty()) b.objects = std::move(p.all); b.identity.subKeys = std::move(p.subKeys); }
    } keep{ base, ps };
    std::unordered_map<const IHavokObject*, std::shared_ptr<io::SchemaObject>> own;
    for (const auto& o : base.objects) own[o.get()] = o;
    for (const auto& [o, id] : base.identity.ids) {
        const auto c = base.identity.category.find(o);
        const bool node = (c != base.identity.category.end() && !c->second.empty()) || std::string(o->ClassName()) == "hkRootLevelContainer";
        if (!node) continue;
        /// @note Ids a previous merge added are already CANONICAL ("bfco:54"); only the base's own FormIds resolve
        ///       through `baseMasters`. Re-resolving a merged-in id as the base's would alias another bundle's node.
        const std::string key = id.find(':') != std::string::npos ? id : baseCanon(id);
        if (auto it = own.find(o); it != own.end()) { ps.byId[key] = it->second; ps.nodes.insert(o); }
    }
    ps.all = base.objects;
    auto all = loadHkyDocs(patch);
    std::vector<std::unique_ptr<HkyDoc>> docs, rosters;
    for (auto& d : all) (d->roster ? rosters : docs).push_back(std::move(d));
    std::vector<std::pair<std::shared_ptr<io::SchemaObject>, bool>> targets;   // (object, patch-mode)
    for (auto& d : docs) {
        const auto r = d->tree.crootref();
        const std::string id = ystr(yget(r, "id"));
        if (auto it = ps.byId.find(ps.canon(id)); it != ps.byId.end()) { targets.push_back({ it->second, true }); if (stats) ++stats->patched; continue; }
        const std::string cls = ystr(yget(r, "class"));
        auto obj = makeObj(reg, cls);
        if (!obj) { err = d->file + ": unknown class '" + cls + "'"; return false; }
        ps.byId[ps.canon(id)] = obj;
        ps.nodes.insert(obj.get());
        ps.all.push_back(obj);
        base.identity.ids[obj.get()] = ps.canon(id);   // owner-qualified: stays unambiguous across later merges
        base.identity.category[obj.get()] = d->cat;
        targets.push_back({ obj, false });
        if (stats) ++stats->added;
    }
    std::unordered_set<const IHavokObject*> fresh;
    for (std::size_t k = 0; k < docs.size(); ++k) {
        ps.where = docs[k]->file;
        ps.patch = targets[k].second;
        if (!targets[k].second) fresh.insert(targets[k].first.get());
        if (!fillHky(*targets[k].first, docs[k]->tree.crootref(), ps)) { err = ps.err; return false; }
    }
    /// @note A roster file may arrive WITHOUT its node's file (a mod that only adds a transition): its owner is then an
    ///       existing node, patched; a node this patch creates takes its rosters whole.
    for (auto& d : rosters) {
        const auto it = ps.byId.find(ps.canon(d->owner));
        io::SchemaObject* owner = it == ps.byId.end() ? nullptr : it->second.get();
        if (!owner) { err = d->file + ": no node '" + d->owner + "' for this roster"; return false; }
        if (!applyRosterFile(*owner, *d, !fresh.count(owner), ps)) { err = ps.err; return false; }
    }
    ps.patch = false;
    int anon = static_cast<int>(base.identity.ids.size());
    for (const auto& o : ps.all)
        if (!base.identity.category.count(o.get())) { base.identity.category[o.get()] = ""; base.identity.ids[o.get()] = "~" + std::to_string(++anon); }
    /// @note Names resolve against the roster REACHABLE from the merged root — a replaced subtree leaves the old
    ///       graph data in the identity, unreachable, and "first found" would read the stale roster.
    if (!appendCharPropBindings(ps)) { err = ps.err; return false; }
    Identity live;
    {
        std::unordered_set<const IHavokObject*> seen;
        std::function<void(const IHavokObject*)> walk = [&](const IHavokObject* o) {
            if (!o || !seen.insert(o).second) return;
            if (auto it = base.identity.category.find(o); it != base.identity.category.end()) live.category[o] = it->second;
            if (auto it = base.identity.ids.find(o); it != base.identity.ids.end()) live.ids[o] = it->second;
            if (const auto* so = dynamic_cast<const io::SchemaObject*>(o))
                for (const io::FieldValue& fv : so->Values()) { walk(fv.obj.get()); for (const auto& e : fv.objs) walk(e.get()); }
        };
        walk(base.root.get());
    }
    const NameResolver nr = buildResolver(live);
    for (const auto& s : ps.symbols) {
        const auto& L = s.isEvent ? nr.eventsL
                      : condTrue(*s.obj, s.obj->Fields()[s.field]->charPropIf) ? nr.charPropsL : nr.variablesL;
        const auto idx = L.encode(s.name);
        if (!idx) {
            int stringDatas = 0, holding = 0;
            for (const auto& [o, c] : base.identity.category)
                if (const auto* sd = dynamic_cast<const io::SchemaObject*>(o); sd && std::string(sd->ClassName()) == "hkbBehaviorGraphStringData") {
                    ++stringDatas;
                    const io::FieldValue* fv = sd->FindField(s.isEvent ? "eventNames" : "variableNames");
                    if (fv && std::find(fv->strs.begin(), fv->strs.end(), s.name) != fv->strs.end()) ++holding;
                }
            err = s.where + ": " + (s.isEvent ? "event" : "variable") + " '" + s.name + "' is not in the merged roster (live roster " +
                  std::to_string(s.isEvent ? nr.events.size() : nr.variables.size()) + " names; " + std::to_string(holding) + "/" +
                  std::to_string(stringDatas) + " string-data objects hold it)";
            return false;
        }
        s.obj->FieldAt(s.field).raw = encInt(static_cast<std::int64_t>(*idx), s.obj->Fields()[s.field]->scalar);
    }
    return true;
}

io::SchemaObject* GraphEdit::GraphData() const {
    const auto* root = m_graph.root.get();
    if (!root) return nullptr;
    for (const auto& e : root->Children("namedVariants")) {
        const auto* entry = dynamic_cast<const io::SchemaObject*>(e.get());
        const auto* graph = entry ? entry->Child("variant") : nullptr;
        if (!graph || std::string(graph->ClassName()) != "hkbBehaviorGraph") continue;
        return graph->Child("data");
    }
    return nullptr;
}

namespace {
/// @brief The graph data's string data (names), created when absent.
io::SchemaObject* stringDataOf(io::SchemaObject& gd, const schema::SchemaRegistry& reg, std::vector<std::shared_ptr<io::SchemaObject>>& own) {
    io::FieldValue& fv = gd.FieldRef("stringData");
    if (!fv.obj) { auto o = makeObj(reg, "hkbBehaviorGraphStringData"); own.push_back(o); fv.obj = o; }
    return dynamic_cast<io::SchemaObject*>(fv.obj.get());
}
std::optional<std::size_t> indexIn(const std::vector<std::string>& names, std::string_view name) {
    for (std::size_t k = 0; k < names.size(); ++k) if (names[k] == name) return k;
    return std::nullopt;
}
} // namespace

std::optional<std::size_t> GraphEdit::VariableIndex(std::string_view name) const {
    auto* gd = GraphData();
    const auto* sd = gd ? gd->Child("stringData") : nullptr;
    const auto* names = strsOf(sd, "variableNames");
    return names ? indexIn(*names, name) : std::nullopt;
}

std::optional<std::size_t> GraphEdit::EventIndex(std::string_view name) const {
    auto* gd = GraphData();
    const auto* sd = gd ? gd->Child("stringData") : nullptr;
    const auto* names = strsOf(sd, "eventNames");
    return names ? indexIn(*names, name) : std::nullopt;
}

GraphEdit::Add GraphEdit::AddVariable(std::string_view name, std::string_view type, std::int32_t word, std::string& err) {
    io::SchemaObject* gd = GraphData();
    if (!gd) { err = "no behavior graph data"; return Add::Failed; }
    const auto& types = en::EnumTable("VariableType");
    const auto t = types.find(std::string(type));
    if (t == types.end()) { err = "unknown variable type '" + std::string(type) + "'"; return Add::Failed; }
    if (isQuadType(t->second) || isPointerType(t->second)) { err = std::string(name) + ": features add word variables only"; return Add::Failed; }
    if (VariableIndex(name)) return Add::Exists;
    io::SchemaObject* sd = stringDataOf(*gd, m_reg, m_graph.objects);
    io::FieldValue& vsv = gd->FieldRef("variableInitialValues");
    if (!vsv.obj) { auto o = makeObj(m_reg, "hkbVariableValueSet"); m_graph.objects.push_back(o); vsv.obj = o; }
    auto* vs = dynamic_cast<io::SchemaObject*>(vsv.obj.get());
    auto info = makeObj(m_reg, "hkbVariableInfo"), role = makeObj(m_reg, "hkbRoleAttribute"), value = makeObj(m_reg, "hkbVariableValue");
    if (!sd || !vs || !info || !role || !value) { err = "vocabulary classes missing from the schema"; return Add::Failed; }
    for (const auto& o : { info, role, value }) m_graph.objects.push_back(o);
    info->SetInt("type", static_cast<std::int8_t>(t->second));
    info->FieldRef("role").obj = role;
    value->SetInt("value", word);
    sd->FieldRef("variableNames").strs.emplace_back(name);
    gd->FieldRef("variableInfos").objs.push_back(info);
    vs->FieldRef("wordVariableValues").objs.push_back(value);
    return Add::Added;
}

GraphEdit::Add GraphEdit::AddQuadVariable(std::string_view name, std::string_view type, const float (&quad)[4],
                                          std::string& err) {
    io::SchemaObject* gd = GraphData();
    if (!gd) { err = "no behavior graph data"; return Add::Failed; }
    const auto& types = en::EnumTable("VariableType");
    const auto t = types.find(std::string(type));
    if (t == types.end()) { err = "unknown variable type '" + std::string(type) + "'"; return Add::Failed; }
    if (!isQuadType(t->second)) { err = std::string(name) + ": '" + std::string(type) + "' is not a quad type"; return Add::Failed; }
    if (VariableIndex(name)) return Add::Exists;
    io::SchemaObject* sd = stringDataOf(*gd, m_reg, m_graph.objects);
    io::FieldValue& vsv = gd->FieldRef("variableInitialValues");
    if (!vsv.obj) { auto o = makeObj(m_reg, "hkbVariableValueSet"); m_graph.objects.push_back(o); vsv.obj = o; }
    auto* vs = dynamic_cast<io::SchemaObject*>(vsv.obj.get());
    auto info = makeObj(m_reg, "hkbVariableInfo"), role = makeObj(m_reg, "hkbRoleAttribute"), value = makeObj(m_reg, "hkbVariableValue");
    if (!sd || !vs || !info || !role || !value) { err = "vocabulary classes missing from the schema"; return Add::Failed; }
    for (const auto& o : { info, role, value }) m_graph.objects.push_back(o);
    std::vector<std::uint8_t>& quads = vs->FieldRef("quadVariableValues").raw;
    info->SetInt("type", static_cast<std::int8_t>(t->second));
    info->FieldRef("role").obj = role;
    value->SetInt("value", static_cast<std::int32_t>(quads.size() / sizeof(quad)));
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(quad);
    quads.insert(quads.end(), bytes, bytes + sizeof(quad));
    sd->FieldRef("variableNames").strs.emplace_back(name);
    gd->FieldRef("variableInfos").objs.push_back(info);
    vs->FieldRef("wordVariableValues").objs.push_back(value);
    return Add::Added;
}

GraphEdit::Add GraphEdit::AddEvent(std::string_view name, std::string& err) {
    io::SchemaObject* gd = GraphData();
    if (!gd) { err = "no behavior graph data"; return Add::Failed; }
    if (EventIndex(name)) return Add::Exists;
    io::SchemaObject* sd = stringDataOf(*gd, m_reg, m_graph.objects);
    auto info = makeObj(m_reg, "hkbEventInfo");
    if (!sd || !info) { err = "vocabulary classes missing from the schema"; return Add::Failed; }
    m_graph.objects.push_back(info);
    sd->FieldRef("eventNames").strs.emplace_back(name);
    gd->FieldRef("eventInfos").objs.push_back(info);
    return Add::Added;
}

void GraphEdit::ForEach(std::string_view cls, const std::function<void(io::SchemaObject&)>& fn) {
    std::unordered_set<const IHavokObject*> seen;
    std::function<void(IHavokObject*)> walk = [&](IHavokObject* o) {
        if (!o || !seen.insert(o).second) return;
        auto* so = dynamic_cast<io::SchemaObject*>(o);
        if (!so) return;
        if (std::string_view(so->ClassName()) == cls) fn(*so);
        for (const io::FieldValue& fv : so->Values()) { walk(fv.obj.get()); for (const auto& e : fv.objs) walk(e.get()); }
    };
    walk(m_graph.root.get());
}

std::size_t GraphEdit::CountDerived(std::string_view base) {
    std::unordered_map<std::string, bool> derives;
    const auto isA = [&](const std::string& cls) {
        const auto [it, fresh] = derives.try_emplace(cls, false);
        if (fresh)
            for (const schema::ClassSchema* c = m_reg.Find(cls); c; c = c->parent.empty() ? nullptr : m_reg.Find(c->parent))
                if (c->name == base) { it->second = true; break; }
        return it->second;
    };
    std::size_t n = 0;
    std::unordered_set<const IHavokObject*> seen;
    std::function<void(IHavokObject*)> walk = [&](IHavokObject* o) {
        if (!o || !seen.insert(o).second) return;
        auto* so = dynamic_cast<io::SchemaObject*>(o);
        if (!so) return;
        if (isA(so->ClassName())) ++n;
        for (const io::FieldValue& fv : so->Values()) { walk(fv.obj.get()); for (const auto& e : fv.objs) walk(e.get()); }
    };
    walk(m_graph.root.get());
    return n;
}

std::shared_ptr<io::SchemaObject> GraphEdit::Make(std::string_view cls) {
    auto o = makeObj(m_reg, std::string(cls));
    if (o) m_graph.objects.push_back(o);
    return o;
}

std::string GraphEdit::IdOf(const IHavokObject* o) const {
    const auto it = m_graph.identity.ids.find(o);
    return it == m_graph.identity.ids.end() || it->second.rfind('~', 0) == 0 ? std::string() : it->second;
}

std::vector<std::string> ReadProjectCharacters(const CB::engine::common::IUnitSource& src, const schema::SchemaRegistry& reg) {
    ParsedHky project;
    std::string err;
    std::vector<std::string> out;
    if (!ParseHkyReflective(src, reg, project, err)) return out;
    GraphEdit(project, reg).ForEach("hkbProjectStringData", [&](io::SchemaObject& so) {
        if (out.empty()) out = so.FieldRef("characterFilenames").strs;
    });
    return out;
}

std::optional<CharacterStrings> ReadCharacterStrings(const CB::engine::common::IUnitSource& src) {
    auto text = src.read("graph.yaml");
    if (!text) return std::nullopt;
    std::string buf = std::move(*text);
    const yml::Tree tree = yml::parse_in_place(c4::to_substr(buf));
    const yml::ConstNodeRef root = tree.crootref();
    if (!root.is_map() || !yhas(root, "namedVariants")) return std::nullopt;
    for (auto e : yget(root, "namedVariants")) {
        if (!e.is_map() || !yhas(e, "variant")) continue;
        const auto v = yget(e, "variant");
        if (!v.is_map() || !yhas(v, "stringData")) continue;
        const auto sd = yget(v, "stringData");
        if (!sd.is_map()) continue;
        CharacterStrings r;
        if (yhas(sd, "name"))             r.name             = ystr(yget(sd, "name"));
        if (yhas(sd, "rigName"))          r.rigName          = ystr(yget(sd, "rigName"));
        if (yhas(sd, "behaviorFilename")) r.behaviorFilename = ystr(yget(sd, "behaviorFilename"));
        if (yhas(sd, "animationNames"))
            for (auto n : yget(sd, "animationNames")) r.animationNames.push_back(ystr(n));
        return r;
    }
    return std::nullopt;
}

std::optional<std::vector<std::string>> ReadSkeletonBones(const CB::engine::common::IUnitSource& src, const schema::SchemaRegistry& reg,
                                                          const std::string& unitPath, const std::string& role, std::string* err) {
    std::set<std::string> present;
    for (const std::string& rel : src.listYaml(role + "/bones", false)) {
        const auto slash = rel.rfind('/');
        present.insert(rel.substr(slash + 1, rel.size() - slash - 6));
    }
    if (present.empty()) return std::nullopt;
    std::vector<std::string> extra;
    if (auto text = src.read(role + "/skeleton.yaml")) {
        std::string buf = std::move(*text);
        const yml::Tree tree = yml::parse_in_place(c4::to_substr(buf));
        const yml::ConstNodeRef root = tree.crootref();
        if (root.is_map() && yhas(root, "extraBones"))
            for (auto e : yget(root, "extraBones")) extra.push_back(ystr(e));
    }
    std::string e;
    auto order = skeletonBoneOrder(reg, unitPath, role, present, extra, e);
    if (!order && err) *err = e;
    return order;
}

bool MergeLayersReflective(const std::vector<CB::engine::common::LayerSource>& layers, const schema::SchemaRegistry& reg,
                           const std::vector<std::string>* bones, ParsedHky& graph, std::string& err) {
    try {
        if (layers.empty() || !layers.front().source) { err = "MergeLayersReflective: no base layer"; return false; }
        if (!ParseHkyReflective(*layers.front().source, reg, graph, err, bones)) { err = layers.front().stem + ": " + err; return false; }
        for (std::size_t k = 1; k < layers.size(); ++k) {
            if (!layers[k].source) continue;
            if (!MergeHkyPatch(graph, *layers[k].source, reg, layers.front().masterTable, layers[k].masterTable, err, bones)) {
                err = layers[k].stem + ": " + err;
                return false;
            }
        }
        return true;
    } catch (const std::exception& e) {
        err = std::string("MergeLayersReflective: ") + e.what();
        return false;
    }
}

bool SerializeGraph(const ParsedHky& graph, std::vector<std::uint8_t>& out, std::string& err, const CB::engine::codec::HKXHeader& header) {
    try {
        PackFileSerializer ser;
        BinaryWriterEx bw(header.Endian == 0, header.PointerSize == 8);
        ser.Serialize(graph.root, bw, header);
        out = bw.Data();
        return true;
    } catch (const std::exception& e) {
        err = std::string("SerializeGraph: ") + e.what();
        return false;
    }
}

bool MergeHkyUnits(const std::vector<std::uint8_t>& baseBytes, const std::vector<std::string>& patchDirs,
                   const schema::SchemaRegistry& reg, const std::string& workDir, std::string& err,
                   const std::vector<std::string>* bones) {
    namespace fs = std::filesystem;
    try {
        std::error_code ec;
        fs::remove_all(workDir, ec);
        PackFileDeserializer desB;
        desB.ObjectFactory = io::MakeSchemaFactory(reg);
        BinaryReaderEx brB(false, true, baseBytes);
        desB.Deserialize(brB);
        const HKXHeader header = desB._header;
        Identity idB = AssignIdentity(desB, reg);
        for (auto& [o, id] : idB.ids) id = formIdOf(CB::engine::codec::formid::SELF_INDEX, id);
        if (!EmitHkyReflective(idB, reg, (fs::path(workDir) / "base").string(), err, bones)) return false;
        ParsedHky merged;
        if (!ParseHkyReflective((fs::path(workDir) / "base").string(), reg, merged, err, bones)) return false;
        for (std::size_t k = 0; k < patchDirs.size(); ++k) {
            const std::string self = "mod" + std::to_string(k + 1);
            if (!MergeHkyPatch(merged, CB::engine::common::DiskUnitSource(patchDirs[k]), reg, { "base" }, { self, "base" }, err, bones)) {
                err = patchDirs[k] + ": " + err;
                return false;
            }
        }
        PackFileSerializer ser;
        BinaryWriterEx bw(header.Endian == 0, header.PointerSize == 8);
        ser.Serialize(merged.root, bw, header);
        const auto out = bw.Data();
        disk::WriteFile(fs::path(workDir) / "merged.hkx", out);
        PackFileDeserializer desM;
        desM.ObjectFactory = io::MakeSchemaFactory(reg);
        BinaryReaderEx brM(false, true, out);
        desM.Deserialize(brM);
        return EmitHkyReflective(AssignIdentity(desM, reg), reg, (fs::path(workDir) / "merged").string(), err, bones);
    } catch (const std::exception& e) {
        err = std::string("MergeHkyUnits: ") + e.what();
        return false;
    }
}

HkyPatchRoundTripResult PatchRoundTrip(const std::vector<std::uint8_t>& baseBytes, const std::vector<std::uint8_t>& modBytes,
                                       const schema::SchemaRegistry& reg, const std::string& workDir,
                                       const std::vector<std::string>* bones) {
    namespace fs = std::filesystem;
    HkyPatchRoundTripResult r;
    try {
        std::error_code ec;
        fs::remove_all(workDir, ec);
        // The base UNIT, as its own bundle stores it (self index 0).
        PackFileDeserializer desB;
        desB.ObjectFactory = io::MakeSchemaFactory(reg);
        { BinaryReaderEx br(false, true, baseBytes); desB.Deserialize(br); }
        Identity idB = AssignIdentity(desB, reg);
        for (auto& [o, id] : idB.ids) id = formIdOf(CB::engine::codec::formid::SELF_INDEX, id);
        if (!EmitHkyReflective(idB, reg, (fs::path(workDir) / "base").string(), r.error, bones)) return r;
        // The mod's PATCH, seen from the mod bundle (self = 0, the base = master 1).
        if (!DeriveHkyPatch(baseBytes, modBytes, reg, (fs::path(workDir) / "patch").string(), r.error, bones, 1, &r.derived)) return r;
        ParsedHky merged;
        if (!ParseHkyReflective((fs::path(workDir) / "base").string(), reg, merged, r.error, bones)) return r;
        if (!MergeHkyPatch(merged, CB::engine::common::DiskUnitSource((fs::path(workDir) / "patch").string()), reg,
                           { "base" }, { "mod", "base" }, r.error, bones, &r.merged)) return r;
        PackFileDeserializer desM;
        desM.ObjectFactory = io::MakeSchemaFactory(reg);
        BinaryReaderEx brM(false, true, modBytes);
        std::shared_ptr<IHavokObject> modRoot = desM.Deserialize(brM);
        const HKXHeader header = desM._header;
        auto serialize = [&](const std::shared_ptr<IHavokObject>& o) {
            PackFileSerializer ser;
            BinaryWriterEx bw(header.Endian == 0, header.PointerSize == 8);
            ser.Serialize(o, bw, header);
            return bw.Data();
        };
        const auto want = serialize(modRoot), got = serialize(merged.root);
        r.identical = (want == got);
        if (!r.identical) {
            disk::WriteFile(fs::path(workDir) / "want.bin", want);
            disk::WriteFile(fs::path(workDir) / "got.bin",  got);
            std::string e2;
            EmitHkyReflective(AssignIdentity(desM, reg), reg, (fs::path(workDir) / "want").string(), e2, bones);
            PackFileDeserializer desG;
            desG.ObjectFactory = io::MakeSchemaFactory(reg);
            BinaryReaderEx brG(false, true, got);
            desG.Deserialize(brG);
            EmitHkyReflective(AssignIdentity(desG, reg), reg, (fs::path(workDir) / "got").string(), e2, bones);
            r.detail = "size " + std::to_string(want.size()) + " -> " + std::to_string(got.size());
        }
        r.ok = true;
    } catch (const std::exception& e) {
        r.error = std::string("PatchRoundTrip: ") + e.what();
    }
    return r;
}

MultiLayerResult MultiLayerRoundTrip(const std::string& baseXml, const std::vector<MultiLayerMod>& mods,
                                     const schema::SchemaRegistry& reg, const std::string& workDir,
                                     const std::vector<std::string>* bones) {
    namespace fs = std::filesystem;
    MultiLayerResult r;
    try {
        std::error_code ec;
        fs::remove_all(workDir, ec);
        const HKXHeader header = HKXHeader::SkyrimSE();
        auto rootOf = [](const ParsedTagfile& p) -> std::shared_ptr<IHavokObject> {
            for (const auto& o : p.objects) if (std::string(o->ClassName()) == "hkRootLevelContainer") return o;
            return nullptr;
        };
        auto serialize = [&](const std::shared_ptr<IHavokObject>& o) {
            PackFileSerializer ser;
            BinaryWriterEx bw(header.Endian == 0, header.PointerSize == 8);
            ser.Serialize(o, bw, header);
            return bw.Data();
        };
        ParsedTagfile base;
        if (!ParseTagfile(baseXml, reg, base, r.error)) { r.error = "base: " + r.error; return r; }
        const auto baseBytes = serialize(rootOf(base));

        // Today's merge of everything, in order (the reference).
        std::vector<std::string> all;
        for (const auto& m : mods) all.insert(all.end(), m.patches.begin(), m.patches.end());
        ParsedTagfile everything; std::vector<std::string> ids;
        if (!MergeTagfiles(baseXml, all, reg, everything, ids, r.error)) { r.error = "merge-all: " + r.error; return r; }
        const auto want = serialize(rootOf(everything));

        // The base unit (self index 0) -> ParseHky; then every mod's derived field patch, in order.
        PackFileDeserializer desB;
        desB.ObjectFactory = io::MakeSchemaFactory(reg);
        { BinaryReaderEx br(false, true, baseBytes); desB.Deserialize(br); }
        Identity idB = AssignIdentity(desB, reg);
        for (auto& [o, id] : idB.ids) id = formIdOf(CB::engine::codec::formid::SELF_INDEX, id);
        if (!EmitHkyReflective(idB, reg, (fs::path(workDir) / "base").string(), r.error, bones)) return r;
        ParsedHky merged;
        if (!ParseHkyReflective((fs::path(workDir) / "base").string(), reg, merged, r.error, bones)) return r;
        for (const auto& m : mods) {
            ParsedTagfile one; std::vector<std::string> oneIds;
            if (!MergeTagfiles(baseXml, m.patches, reg, one, oneIds, r.error)) { r.error = m.name + ": " + r.error; return r; }
            const fs::path pd = fs::path(workDir) / ("patch_" + m.name);
            HkyPatchStats st;
            ParsedHky vanilla;
            if (!ParseHkyReflective((fs::path(workDir) / "base").string(), reg, vanilla, r.error, bones) ||
                !DeriveHkyPatch(vanilla, { "base" }, { m.name, "base" }, serialize(rootOf(one)), reg, pd.string(), r.error, bones, &st)) {
                r.error = m.name + ": " + r.error;
                return r;
            }
            if (!MergeHkyPatch(merged, CB::engine::common::DiskUnitSource(pd.string()), reg, { "base" }, { m.name, "base" }, r.error, bones)) {
                r.error = m.name + ": " + r.error;
                std::string e2;   // the merged state at failure, for inspection
                EmitHkyReflective(merged.identity, reg, (fs::path(workDir) / ("failed_after_" + m.name)).string(), e2, bones);
                return r;
            }
            r.perMod.push_back({ m.name, st });
            /// @note Debug knob: CB_MULTILAYER_TRACE=1 dumps the merged state after each mod (after_<mod>/) and
            ///       that mod's single-mod reference merge (one_<mod>/, what its patch was derived from).
            if (std::getenv("CB_MULTILAYER_TRACE")) {
                std::string e2;
                EmitHkyReflective(merged.identity, reg, (fs::path(workDir) / ("after_" + m.name)).string(), e2, bones);
                PackFileDeserializer d1;
                d1.ObjectFactory = io::MakeSchemaFactory(reg);
                BinaryReaderEx br1(false, true, serialize(rootOf(one)));
                d1.Deserialize(br1);
                EmitHkyReflective(AssignIdentity(d1, reg), reg, (fs::path(workDir) / ("one_" + m.name)).string(), e2, bones);
            }
        }
        const auto got = serialize(merged.root);
        r.identical = (got == want);
        if (!r.identical) {
            disk::WriteFile(fs::path(workDir) / "want.bin", want);
            disk::WriteFile(fs::path(workDir) / "got.bin",  got);
            std::string e2;
            for (const auto& [name, bytes] : { std::pair<const char*, const std::vector<std::uint8_t>*>{ "want", &want }, { "got", &got } }) {
                PackFileDeserializer d;
                d.ObjectFactory = io::MakeSchemaFactory(reg);
                BinaryReaderEx br(false, true, *bytes);
                d.Deserialize(br);
                EmitHkyReflective(AssignIdentity(d, reg), reg, (fs::path(workDir) / name).string(), e2, bones);
            }
            r.detail = "size " + std::to_string(want.size()) + " -> " + std::to_string(got.size());
        }
        r.ok = true;
    } catch (const std::exception& e) {
        r.error = std::string("MultiLayerRoundTrip: ") + e.what();
    }
    return r;
}

HkyRoundTripResult RoundTripReflective(const std::vector<std::uint8_t>& bytes, const schema::SchemaRegistry& reg,
                                       const std::string& workDir, const std::vector<std::string>* bones,
                                       const std::string& unitPath) {
    HkyRoundTripResult r;
    try {
        PackFileDeserializer des;
        des.ObjectFactory = io::MakeSchemaFactory(reg);
        BinaryReaderEx br(false, true, bytes);
        std::shared_ptr<IHavokObject> root = des.Deserialize(br);
        const HKXHeader header = des._header;
        auto serialize = [&](const std::shared_ptr<IHavokObject>& o) {
            PackFileSerializer ser;
            BinaryWriterEx bw(header.Endian == 0, header.PointerSize == 8);
            ser.Serialize(o, bw, header);
            return bw.Data();
        };
        const std::vector<std::uint8_t> canon = serialize(root);
        r.canonIsSource = (canon == bytes);
        std::error_code ec;
        std::filesystem::remove_all(workDir, ec);
        if (!EmitHkyReflective(AssignIdentity(des, reg), reg, workDir, r.error, bones, unitPath)) return r;
        ParsedHky parsed;
        if (!ParseHkyReflective(workDir, reg, parsed, r.error, bones, unitPath)) return r;
        const std::vector<std::uint8_t> back = serialize(parsed.root);
        r.identical = (back == canon);
        if (!r.identical) {
            const std::size_t n = std::min(back.size(), canon.size());
            std::size_t i = 0;
            while (i < n && back[i] == canon[i]) ++i;
            r.firstDiff = i;
            r.sizeDelta = static_cast<long long>(back.size()) - static_cast<long long>(canon.size());
            disk::WriteFile(std::filesystem::path(workDir) / "canon.bin", canon);
            disk::WriteFile(std::filesystem::path(workDir) / "back.bin", back);
            // Diagnose by topology: re-read `back` and compare per-class object counts with the source graph.
            auto histogram = [](PackFileDeserializer& d) {
                std::map<std::string, int> h;
                for (const auto& [off, o] : d.DeserializedObjects()) if (o) ++h[o->ClassName()];
                return h;
            };
            PackFileDeserializer des2;
            des2.ObjectFactory = io::MakeSchemaFactory(reg);
            BinaryReaderEx br2(false, true, back);
            des2.Deserialize(br2);
            const auto a = histogram(des), b = histogram(des2);
            std::set<std::string> classes;
            for (const auto& [k, n] : a) classes.insert(k);
            for (const auto& [k, n] : b) classes.insert(k);
            for (const auto& k : classes) {
                const int x = a.count(k) ? a.at(k) : 0, y = b.count(k) ? b.at(k) : 0;
                if (x != y) r.detail += (r.detail.empty() ? "" : ", ") + k + ":" + std::to_string(x) + "->" + std::to_string(y);
            }
        }
        r.ok = true;
    } catch (const std::exception& e) {
        r.error = std::string("RoundTripReflective: ") + e.what();
    }
    return r;
}

bool ParseTagfile(const std::string& xmlText, const schema::SchemaRegistry& reg,
                  ParsedTagfile& out, std::string& err) {
    return parseSourcesMerged({ { &xmlText, false } }, reg, out, nullptr, err);
}

bool MergeTagfiles(const std::string& baseXml, const std::vector<std::string>& patchXmls,
                   const schema::SchemaRegistry& reg, ParsedTagfile& out,
                   std::vector<std::string>& deltaIds, std::string& err) {
    // Precedence: base first, then each patch in load order (later overrides earlier).
    std::vector<std::pair<const std::string*, bool>> sources;
    sources.reserve(1 + patchXmls.size());
    sources.push_back({ &baseXml, false });
    for (const std::string& p : patchXmls) sources.push_back({ &p, true });
    deltaIds.clear();
    return parseSourcesMerged(sources, reg, out, &deltaIds, err);
}

ModDeltaResult ConvertModDelta(const std::string& baseTagfileXml, const std::vector<std::string>& masterPatchDirs,
                               const std::vector<std::string>& patchDirs, const ParsedHky& base,
                               const std::vector<std::string>& baseMasters, const std::vector<std::string>& patchMasters,
                               const schema::SchemaRegistry& reg, const std::string& outDeltaDir,
                               const std::vector<std::string>* bones) {
    namespace fs = std::filesystem;
    ModDeltaResult r;
    /// @note RAW patch text (MOD_CODE markers intact): parseSourcesMerged strips for the node parse AND reads the
    ///       markers, so the merge takes only each mod's ACTUAL edits.
    auto readPatches = [](const std::vector<std::string>& dirs, std::vector<std::string>& out) {
        std::error_code ec;
        for (const std::string& dir : dirs) {
            if (!fs::is_directory(dir, ec)) continue;
            std::vector<fs::path> files;
            for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
                if (!it->is_regular_file()) continue;
                const std::string fn = it->path().filename().string();
                if (!fn.empty() && fn[0] == '#' && codec::resource::HasExt(fn, ".txt")) files.push_back(it->path());
            }
            std::sort(files.begin(), files.end());
            for (const fs::path& f : files)
                if (auto t = disk::ReadFile(f); t && !t->empty()) out.push_back(std::move(*t));
        }
    };
    std::vector<std::string> patches;
    readPatches(masterPatchDirs, patches);
    const std::size_t fromMasters = patches.size();
    readPatches(patchDirs, patches);
    r.patchNodes = static_cast<int>(patches.size() - fromMasters);
    if (r.patchNodes == 0) { r.ok = true; return r; }

    try {
        ParsedTagfile merged; std::vector<std::string> deltaIds; std::string err;
        if (!MergeTagfiles(baseTagfileXml, patches, reg, merged, deltaIds, err)) { r.error = "merge: " + err; return r; }
        std::shared_ptr<IHavokObject> root;
        for (const auto& o : merged.objects) if (std::string(o->ClassName()) == "hkRootLevelContainer") { root = o; break; }
        if (!root) { r.error = "merge: no root container"; return r; }
        const HKXHeader header = HKXHeader::SkyrimSE();
        PackFileSerializer ser;
        BinaryWriterEx bw(header.Endian == 0, header.PointerSize == 8);
        ser.Serialize(root, bw, header);
        HkyPatchStats st;
        if (!DeriveHkyPatch(base, baseMasters, patchMasters, bw.Data(), reg, outDeltaDir, err, bones, &st)) {
            r.error = "derive: " + err;
            return r;
        }
        r.patched = st.patched;
        r.added   = st.added;
        r.ok = true;
    } catch (const std::exception& e) {
        r.error = std::string("ConvertModDelta: ") + e.what();
    }
    return r;
}

} // namespace CB::engine::decompile
