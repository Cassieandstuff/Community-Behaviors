#include <decompile/BehaviorDecompile.h>

#include <decompile/TagfileOracle.h>      // AlignTagfile — the structural #NNNN oracle (reused verbatim)
#include <interface/HavokEnums.h>       // enum tables (value<->name), revNum, FormatFlags, ResolveEnum — reused
#include <common/Xml.h>                // first-party tagfile XML parser (xml::Node / xml::Parse)
#include <decompile/PandoraCompatShim.h> // CONVERTER-ONLY Nemesis text-array placement (Pandora parity)
#include <interface/BashMerge.h>        // shared merge core (bashMerge / decideParam / changedFields) — lockstep w/ runtime
#include <interface/linker/Linker.h>      // the ONE name<->index codec (roster decode + dup-guard)
#include <codec/formid/FormId.h>          // CB::core::codec::formid — node identity as a FormId "idx:local"
#include <common/Vec4Text.h>              // CB::core::common::vec4::parseVec4 (the ONE vec4 text parser)
#include <RymlInclude.h>                  // ParseHky — the reflective .hky reader
#include <codec/serialization/packfile/PackFileSerializer.h>   // RoundTripReflective re-serialize

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// Stage 3. Increment 1 scaffolded the lib; increment 2 (here) is the identity/index pass +
// the class→category map. Increment 3 fills in EmitHky. See HavokModel.h for the architecture.

namespace CB::core::decompile {
using namespace CB::core::codec;
using namespace CB::core::common;

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
        {"hkbManualSelectorGenerator", "selectors"},
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
    };
    auto it = kMap.find(className);
    return it == kMap.end() ? std::string{} : it->second;
}

Identity AssignIdentity(PackFileDeserializer& des, const schema::SchemaRegistry& /*reg*/,
                        const std::string& xmlText) {
    Identity out;
    const auto& byOff = des.DeserializedObjects();   // offset -> object

    if (!xmlText.empty()) {
        // Tagfile-aligned ids (matches Skyrim.hky): recover #NNNN per binary offset via the structural
        // oracle, then key by object. This is the identity/index step for vanilla content.
        const CB::core::decompile::OracleResult res = CB::core::decompile::AlignTagfile(des, xmlText);
        for (const auto& [off, id] : res.off2id) {
            auto it = byOff.find(off);
            if (it == byOff.end()) continue;
            IHavokObject* obj = it->second.get();
            out.ids[obj]      = std::to_string(id);   // canonical: unpadded decimal
            out.category[obj] = CategoryForClass(obj->ClassName());
        }
        return out;
    }

    // Encounter-order fallback (no XML): post-order numbering for a self-consistent round-trip. Base =
    // ClassnamesCount (approx; not the true tagfile base) — filenames won't match Skyrim.hky, but the
    // hkx→hky→hkx round-trip stays internally consistent.
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
namespace en = CB::core::common::enums;
using schema::Field;
using schema::FieldKind;
using schema::Scalar;

std::string fstr(float v) { char b[32]; std::snprintf(b, sizeof b, "%.9g", v); return b; }
std::string boolstr(bool v) { return v ? "true" : "false"; }
// enum value -> name, else the raw number (round-trips: numbers parse back). Via the one enum codec
// (enums::enumName), so this and the decompiler's revNum are one rule — and deterministic on aliases
// (B5), unlike the old arbitrary unordered_map scan.
std::string revNum(const std::unordered_map<std::string, long>& t, long v) {
    std::string nm = en::enumName(v, t);
    return nm.empty() ? std::to_string(v) : nm;
}

// YAML single-quote (double any '); double-quote+escape if control chars present. Byte-identical to
// the decompiler's q().
std::string q(const std::string& s) {
    bool ctrl = false;
    for (unsigned char c : s) if (c < 0x20) { ctrl = true; break; }
    if (!ctrl) {
        std::string o = "'";
        for (char c : s) { if (c == '\'') o += "''"; else o += c; }
        return o + "'";
    }
    std::string o = "\"";
    char buf[8];
    for (unsigned char c : s) {
        switch (c) {
            case '"':  o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n";  break;
            case '\r': o += "\\r";  break;
            case '\t': o += "\\t";  break;
            default:
                if (c < 0x20) { std::snprintf(buf, sizeof buf, "\\x%02X", c); o += buf; }
                else o += static_cast<char>(c);
        }
    }
    return o + "\"";
}

// enum type name -> table. Extended as categories need more; the decompiler references these by the
// same names (en::SelfTransitionMode() etc.).
/// @note Resolves through the schema-generated tables (en::EnumTable, baked from Havok/core/Schema/metadata/enums),
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

// Base fields handled by the node header / bindings block rather than the generic field loop.
bool isHeaderField(const std::string& name) {
    return name == "name" || name == "userData" || name == "enable" || name == "variableBindingSet";
}

std::string renderScalar(const Field& f, const io::FieldValue& v) {
    if (f.scalar == Scalar::Float) return fstr(decodeFloat(v.raw));
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
    // Bones/events/vars match EXACTLY (Havok is case-sensitive) — the linker's default (no case-fold).
    CB::core::linker::Linker eventsL, variablesL, charPropsL;
    void index() {   // call after the roster vectors are filled
        namespace linker = CB::core::linker;
        eventsL    = linker::Linker::OfOrderedNames(events);
        variablesL = linker::Linker::OfOrderedNames(variables);
        charPropsL = linker::Linker::OfOrderedNames(charProps);
    }
    // index -> name (decode). "" out of range (caller keeps the raw number) — byte-identical to the
    // former bounds-checked vector read (decode(i) == roster[i]).
    std::string event(long i) const    { return eventsL.decode(i).value_or(std::string{}); }
    std::string variable(long i) const { return variablesL.decode(i).value_or(std::string{}); }
    std::string charProp(long i) const { return charPropsL.decode(i).value_or(std::string{}); }
    // Merge-safe name: only if `i` is the FIRST index carrying that name (a duplicate would remap on
    // round-trip, so the decompiler falls back to the raw id). encode() returns the first index for a
    // name, so "first occurrence" is exactly encode(name) == i — byte-identical to the old scan.
    std::string safe(const CB::core::linker::Linker& L, long i) const {
        const auto nm = L.decode(i);
        if (!nm || nm->empty()) return {};
        const auto first = L.encode(*nm);
        return (first && *first == static_cast<CB::core::linker::Value>(i)) ? *nm : std::string{};
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
        const auto gf = gd->Fields();
        const auto& gv = gd->Values();
        for (std::size_t i = 0; i < gf.size(); ++i)
            if (gf[i]->name == "stringData" && gv[i].obj)
                target = dynamic_cast<const io::SchemaObject*>(gv[i].obj.get());
        break;
    }
    for (const auto& [obj, cat] : identity.category) {
        if (std::string(obj->ClassName()) != "hkbBehaviorGraphStringData") continue;
        const auto* so = dynamic_cast<const io::SchemaObject*>(obj);
        if (!so) continue;
        if (target && so != target) continue;   // prefer the graph-data's own string-data
        const auto flds = so->Fields();
        const auto& vs = so->Values();
        for (std::size_t i = 0; i < flds.size(); ++i) {
            const std::string& n = flds[i]->name;
            if      (n == "eventNames")             nr.events     = vs[i].strs;
            else if (n == "variableNames")          nr.variables  = vs[i].strs;
            else if (n == "attributeNames")         nr.attributes = vs[i].strs;
            else if (n == "characterPropertyNames") nr.charProps  = vs[i].strs;
        }
        break;   // one string-data table per graph
    }
    nr.index();   // build the name<->index codecs from the parsed rosters
    return nr;
}

// field lookup on a SchemaObject
const io::FieldValue* fieldByName(const io::SchemaObject& so, const std::string& name, const Field** outF = nullptr) {
    const auto flds = so.Fields();
    const auto& vs = so.Values();
    for (std::size_t i = 0; i < flds.size(); ++i)
        if (flds[i]->name == name) { if (outF) *outF = flds[i]; return &vs[i]; }
    return nullptr;
}

// hkbStringEventPayload.data (the only payload kind we carry), else "".
std::string payloadStr(const std::shared_ptr<IHavokObject>& p) {
    if (!p) return {};
    const auto* so = dynamic_cast<const io::SchemaObject*>(p.get());
    if (!so || std::string(so->ClassName()) != "hkbStringEventPayload") return {};
    const io::FieldValue* d = fieldByName(*so, "data");
    return d ? d->str : std::string();
}

// hkbEventProperty inline: `event: '<name>'` (+ `payload:`) — NOT its raw id/payload fields.
// Recurring special (triggers, notify events, modifier events). `indent` is the key indent.
std::string renderEventProperty(const io::SchemaObject& ev, const NameResolver& nr, const std::string& indent) {
    std::string out;
    const io::FieldValue* idv = fieldByName(ev, "id");
    const io::FieldValue* pl  = fieldByName(ev, "payload");
    long id = idv ? decodeInt(idv->raw, Scalar::Int32) : -1;
    const std::string en = nr.event(id);
    if (!en.empty()) out += indent + "event: " + q(en) + "\n";
    if (pl && pl->obj) { const std::string ps = payloadStr(pl->obj); out += indent + "payload: " + q(ps) + "\n"; }
    return out;
}

// ── typed field getters over a SchemaObject ───────────────────────────────────
long  fInt  (const io::SchemaObject& so, const char* n) { const Field* f=nullptr; auto* v=fieldByName(so,n,&f); return (v&&f)?static_cast<long>(decodeInt(v->raw,f->scalar)):0; }  // fInt stays long (small fields); explicit cast, no #6 truncation concern
float fFloat(const io::SchemaObject& so, const char* n) { auto* v=fieldByName(so,n); return v?decodeFloat(v->raw):0.f; }
bool  fBool (const io::SchemaObject& so, const char* n) { auto* v=fieldByName(so,n); return v&&!v->raw.empty()&&v->raw[0]!=0; }
std::string fStr(const io::SchemaObject& so, const char* n) { auto* v=fieldByName(so,n); return v?v->str:std::string(); }
std::shared_ptr<IHavokObject> fPtr(const io::SchemaObject& so, const char* n) { auto* v=fieldByName(so,n); return v?v->obj:nullptr; }
// id of a referenced node (or "" if null / not identified). In refsAsNames mode (diff only) a named
// target renders as `Class:name` — the stable cross-compile identity — with the numeric id as the
// fallback for an unnamed target. Production emits (refsAsNames=false) render the canonical #NNNN.
std::string refIdOf(const std::shared_ptr<IHavokObject>& p, const Identity& id) {
    if (!p) return {};
    if (id.refsAsNames) {
        if (const auto* so = dynamic_cast<const io::SchemaObject*>(p.get())) {
            const io::FieldValue* nf = fieldByName(*so, "name");
            if (nf && !nf->str.empty()) return std::string(so->ClassName()) + ":" + nf->str;
            // Unnamed target: use a secondary identity that is stable across compiles. A clip has no
            // name but its animationName is unique+stable, so a ref to an unnamed clip still aligns.
            if (const io::FieldValue* an = fieldByName(*so, "animationName"); an && !an->str.empty())
                return std::string(so->ClassName()) + ":@" + an->str;
        }
    }
    auto it = id.ids.find(p.get());
    return it == id.ids.end() ? std::string() : it->second;
}
// merge-safe event/variable name companion line (only when uniquely nameable).
void evLine (std::string& y, const std::string& ind, const char* key, long id,  const NameResolver& nr) { const std::string n=nr.safeEvent(id);    if(!n.empty()) y+=ind+key+": "+q(n)+"\n"; }
void varLine(std::string& y, const std::string& ind, const char* key, long idx, const NameResolver& nr) { const std::string n=nr.safeVariable(idx); if(!n.empty()) y+=ind+key+": "+q(n)+"\n"; }

std::string bindingsBlock(const std::shared_ptr<IHavokObject>& bsP, const NameResolver& nr, const std::string& ind) {
    const auto* bs = dynamic_cast<const io::SchemaObject*>(bsP.get());
    if (!bs) return {};
    const io::FieldValue* bl = fieldByName(*bs, "bindings");
    if (!bl || bl->objs.empty()) return {};
    const long enIdx = fInt(*bs, "indexOfBindingToEnable");
    std::string y = ind + "bindings:\n";
    for (int i = 0; i < static_cast<int>(bl->objs.size()); ++i) {
        const auto* b = dynamic_cast<const io::SchemaObject*>(bl->objs[i].get());
        if (!b) continue;
        y += ind + "  - memberPath: " + q(fStr(*b,"memberPath")) + "\n";
        const long vi = fInt(*b,"variableIndex");
        y += ind + "    variableIndex: " + std::to_string(vi) + "\n";
        const long bt = fInt(*b,"bindingType");
        const std::string vn = (bt == 1) ? nr.charProp(vi) : nr.variable(vi);
        if (!vn.empty()) y += ind + "    variable: " + q(vn) + "\n";
        y += ind + "    bitIndex: " + std::to_string(fInt(*b,"bitIndex")) + "\n";
        y += ind + "    bindingType: " + revNum(en::BindingType(), bt) + "\n";
        if (i == enIdx) y += ind + "    enableTarget: true\n";
    }
    return y;
}

std::string intervalBlock(const io::SchemaObject& iv, const std::string& ind, const NameResolver& nr) {
    std::string y;
    const long en1 = fInt(iv,"enterEventId"); y += ind + "  enterEventId: " + std::to_string(en1) + "\n"; evLine(y, ind+"  ", "enterEvent", en1, nr);
    const long ex1 = fInt(iv,"exitEventId");  y += ind + "  exitEventId: "  + std::to_string(ex1) + "\n"; evLine(y, ind+"  ", "exitEvent",  ex1, nr);
    y += ind + "  enterTime: " + fstr(fFloat(iv,"enterTime")) + "\n";
    y += ind + "  exitTime: "  + fstr(fFloat(iv,"exitTime"))  + "\n";
    return y;
}

std::string transitionsBlock(const std::shared_ptr<IHavokObject>& arrP, const Identity& id, const NameResolver& nr, const std::string& ind) {
    const auto* arr = dynamic_cast<const io::SchemaObject*>(arrP.get());
    if (!arr) return {};
    const io::FieldValue* tv = fieldByName(*arr, "transitions");
    if (!tv || tv->objs.empty()) return {};
    std::string y = ind + "transitions:\n";
    for (const auto& te : tv->objs) {
        const auto* t = dynamic_cast<const io::SchemaObject*>(te.get());
        if (!t) continue;
        const io::FieldValue* tiv = fieldByName(*t,"triggerInterval");
        const io::FieldValue* iiv = fieldByName(*t,"initiateInterval");
        if (tiv && tiv->obj) if (auto* o=dynamic_cast<const io::SchemaObject*>(tiv->obj.get())) y += ind + "  - triggerInterval:\n" + intervalBlock(*o, ind+"    ", nr);
        if (iiv && iiv->obj) if (auto* o=dynamic_cast<const io::SchemaObject*>(iiv->obj.get())) y += ind + "    initiateInterval:\n" + intervalBlock(*o, ind+"    ", nr);
        if (auto tr = fPtr(*t,"transition")) { const std::string r=refIdOf(tr,id); if(!r.empty()) y += ind + "    transition: " + r + "\n"; }
        if (auto c = fPtr(*t,"condition")) {
            const auto* co = dynamic_cast<const io::SchemaObject*>(c.get());
            if (co) {
                const std::string cc = co->ClassName();
                if (cc == "hkbExpressionCondition") y += ind + "    condition: " + q(fStr(*co,"expression")) + "\n";
                else if (cc == "hkbStringCondition") y += ind + "    conditionString: " + q(fStr(*co,"conditionString")) + "\n";
            }
        }
        const long ev = fInt(*t,"eventId"); y += ind + "    eventId: " + std::to_string(ev) + "\n"; evLine(y, ind+"    ", "event", ev, nr);
        y += ind + "    toStateId: "         + std::to_string(fInt(*t,"toStateId"))         + "\n";
        y += ind + "    fromNestedStateId: " + std::to_string(fInt(*t,"fromNestedStateId")) + "\n";
        y += ind + "    toNestedStateId: "   + std::to_string(fInt(*t,"toNestedStateId"))   + "\n";
        y += ind + "    priority: "          + std::to_string(fInt(*t,"priority"))          + "\n";
        y += ind + "    flags: " + en::FormatFlags(fInt(*t,"flags"), en::TransitionFlags()) + "\n";
    }
    return y;
}

std::string notifyBlock(const std::shared_ptr<IHavokObject>& arrP, const char* key, const NameResolver& nr) {
    const auto* arr = dynamic_cast<const io::SchemaObject*>(arrP.get());
    if (!arr) return {};
    const io::FieldValue* ev = fieldByName(*arr, "events");
    if (!ev || ev->objs.empty()) return {};
    std::string y = std::string(key) + ":\n";
    for (const auto& ee : ev->objs) {
        const auto* e = dynamic_cast<const io::SchemaObject*>(ee.get());
        if (!e) continue;
        const long id = fInt(*e,"id");
        y += "  - id: " + std::to_string(id) + "\n";
        const std::string n = nr.safeEvent(id); if (!n.empty()) y += "    event: " + q(n) + "\n";
        if (auto pl = fPtr(*e,"payload")) y += "    payload: " + q(payloadStr(pl)) + "\n";
    }
    return y;
}

std::string boneWeightsBlock(const std::shared_ptr<IHavokObject>& bwP, const std::string& ind) {
    const auto* bw = dynamic_cast<const io::SchemaObject*>(bwP.get());
    if (!bw) return {};
    const io::FieldValue* w = fieldByName(*bw, "boneWeights");   // scalararray float (raw bytes)
    if (!w) return {};
    const std::size_t n = w->raw.size() / 4;
    std::string vals;
    for (std::size_t i = 0; i < n; ++i) { if (i) vals += ' '; float f; std::memcpy(&f, w->raw.data() + i*4, 4); vals += fstr(f); }
    return ind + "boneWeights:\n" + ind + "  count: " + std::to_string(n) + "\n" + ind + "  values: " + q(vals) + "\n";
}

std::string pvecRaw(const std::vector<std::uint8_t>& raw);  // fwd
std::string modifierEvent(const io::SchemaObject& ev, const NameResolver& nr, const std::string& key);  // fwd

// Shared blender/pose-matching `children:` block (hkbBlenderGeneratorChild elements).
std::string blenderChildren(const io::SchemaObject& so, const Identity& id, const NameResolver& nr) {
    std::string y = "children:\n";
    const io::FieldValue* ch = fieldByName(so, "children");
    if (ch) for (const auto& ce : ch->objs) {
        const auto* c = dynamic_cast<const io::SchemaObject*>(ce.get());
        if (!c) continue;
        y += "  - generator: " + refIdOf(fPtr(*c,"generator"), id) + "\n";
        y += "    weight: " + fstr(fFloat(*c,"weight")) + "\n";
        y += "    worldFromModelWeight: " + fstr(fFloat(*c,"worldFromModelWeight")) + "\n";
        if (auto bw = fPtr(*c,"boneWeights")) y += boneWeightsBlock(bw, "    ");
        y += bindingsBlock(fPtr(*c,"variableBindingSet"), nr, "    ");
    }
    return y;
}

std::string renderBlender(const io::SchemaObject& so, const Identity& id, const NameResolver& nr) {
    std::string y = "class: hkbBlenderGenerator\n";
    y += "name: " + q(fStr(so,"name")) + "\n";
    y += "userData: " + std::to_string(fInt(so,"userData")) + "\n";
    y += "referencePoseWeightThreshold: " + fstr(fFloat(so,"referencePoseWeightThreshold")) + "\n";
    y += "blendParameter: " + fstr(fFloat(so,"blendParameter")) + "\n";
    y += "minCyclicBlendParameter: " + fstr(fFloat(so,"minCyclicBlendParameter")) + "\n";
    y += "maxCyclicBlendParameter: " + fstr(fFloat(so,"maxCyclicBlendParameter")) + "\n";
    y += "indexOfSyncMasterChild: " + std::to_string(fInt(so,"indexOfSyncMasterChild")) + "\n";
    y += "flags: " + en::FormatFlags(fInt(so,"flags"), en::BlenderFlags()) + "\n";
    y += "subtractLastChild: " + boolstr(fBool(so,"subtractLastChild")) + "\n";
    y += bindingsBlock(fPtr(so,"variableBindingSet"), nr, "");
    y += blenderChildren(so, id, nr);
    return y;
}

std::string renderOffsetAnim(const io::SchemaObject& so, const Identity& id, const NameResolver& nr) {
    std::string y = "class: BSOffsetAnimationGenerator\n";
    y += "name: " + q(fStr(so,"name")) + "\n";
    y += "userData: " + std::to_string(fInt(so,"userData")) + "\n";
    if (auto g = fPtr(so,"pDefaultGenerator"))    { const std::string r=refIdOf(g,id); if(!r.empty()) y += "pDefaultGenerator: " + r + "\n"; }
    if (auto g = fPtr(so,"pOffsetClipGenerator")) { const std::string r=refIdOf(g,id); if(!r.empty()) y += "pOffsetClipGenerator: " + r + "\n"; }
    y += "fOffsetVariable: " + fstr(fFloat(so,"fOffsetVariable")) + "\n";
    y += "fOffsetRangeStart: " + fstr(fFloat(so,"fOffsetRangeStart")) + "\n";
    y += "fOffsetRangeEnd: " + fstr(fFloat(so,"fOffsetRangeEnd")) + "\n";
    y += bindingsBlock(fPtr(so,"variableBindingSet"), nr, "");
    return y;
}

std::string renderSyncClip(const io::SchemaObject& so, const Identity& id, const NameResolver& nr) {
    std::string y = "class: BSSynchronizedClipGenerator\n";
    y += "name: " + q(fStr(so,"name")) + "\n";
    y += "userData: " + std::to_string(fInt(so,"userData")) + "\n";
    if (auto g = fPtr(so,"pClipGenerator")) { const std::string r=refIdOf(g,id); if(!r.empty()) y += "pClipGenerator: " + r + "\n"; }
    y += "SyncAnimPrefix: " + q(fStr(so,"SyncAnimPrefix")) + "\n";
    y += "bSyncClipIgnoreMarkPlacement: " + boolstr(fBool(so,"bSyncClipIgnoreMarkPlacement")) + "\n";
    y += "fGetToMarkTime: " + fstr(fFloat(so,"fGetToMarkTime")) + "\n";
    y += "fMarkErrorThreshold: " + fstr(fFloat(so,"fMarkErrorThreshold")) + "\n";
    y += "bLeadCharacter: " + boolstr(fBool(so,"bLeadCharacter")) + "\n";
    y += "bReorientSupportChar: " + boolstr(fBool(so,"bReorientSupportChar")) + "\n";
    y += "bApplyMotionFromRoot: " + boolstr(fBool(so,"bApplyMotionFromRoot")) + "\n";
    y += "sAnimationBindingIndex: " + std::to_string(fInt(so,"sAnimationBindingIndex")) + "\n";
    y += bindingsBlock(fPtr(so,"variableBindingSet"), nr, "");
    return y;
}

std::string renderReferencePose(const io::SchemaObject& so, const Identity&, const NameResolver& nr) {
    std::string y = "class: hkbReferencePoseGenerator\n";
    y += "name: " + q(fStr(so,"name")) + "\n";
    y += "userData: " + std::to_string(fInt(so,"userData")) + "\n";
    y += bindingsBlock(fPtr(so,"variableBindingSet"), nr, "");
    return y;
}

std::string renderGamebryo(const io::SchemaObject& so, const Identity&, const NameResolver& nr) {
    std::string y = "class: BGSGamebryoSequenceGenerator\n";
    y += "name: " + q(fStr(so,"name")) + "\n";
    y += "userData: " + std::to_string(fInt(so,"userData")) + "\n";
    y += "sequence: " + q(fStr(so,"pSequence")) + "\n";
    y += "blendModeFunction: " + revNum(en::BlendModeFunction(), fInt(so,"eBlendModeFunction")) + "\n";
    y += "percent: " + fstr(fFloat(so,"fPercent")) + "\n";
    y += bindingsBlock(fPtr(so,"variableBindingSet"), nr, "");
    return y;
}

std::string renderPoseMatching(const io::SchemaObject& so, const Identity& id, const NameResolver& nr) {
    std::string y = "class: hkbPoseMatchingGenerator\n";
    y += "name: " + q(fStr(so,"name")) + "\n";
    y += "userData: " + std::to_string(fInt(so,"userData")) + "\n";
    y += "referencePoseWeightThreshold: " + fstr(fFloat(so,"referencePoseWeightThreshold")) + "\n";
    y += "blendParameter: " + fstr(fFloat(so,"blendParameter")) + "\n";
    y += "minCyclicBlendParameter: " + fstr(fFloat(so,"minCyclicBlendParameter")) + "\n";
    y += "maxCyclicBlendParameter: " + fstr(fFloat(so,"maxCyclicBlendParameter")) + "\n";
    y += "indexOfSyncMasterChild: " + std::to_string(fInt(so,"indexOfSyncMasterChild")) + "\n";
    y += "flags: " + en::FormatFlags(fInt(so,"flags"), en::BlenderFlags()) + "\n";
    y += "subtractLastChild: " + boolstr(fBool(so,"subtractLastChild")) + "\n";
    y += bindingsBlock(fPtr(so,"variableBindingSet"), nr, "");
    y += blenderChildren(so, id, nr);
    const io::FieldValue* wr = fieldByName(so,"worldFromModelRotation");
    y += "worldFromModelRotation: " + pvecRaw(wr ? wr->raw : std::vector<std::uint8_t>{}) + "\n";
    y += "blendSpeed: " + fstr(fFloat(so,"blendSpeed")) + "\n";
    y += "minSpeedToSwitch: " + fstr(fFloat(so,"minSpeedToSwitch")) + "\n";
    y += "minSwitchTimeNoError: " + fstr(fFloat(so,"minSwitchTimeNoError")) + "\n";
    y += "minSwitchTimeFullError: " + fstr(fFloat(so,"minSwitchTimeFullError")) + "\n";
    const long sp = fInt(so,"startPlayingEventId");  y += "startPlayingEventId: " + std::to_string(sp) + "\n"; evLine(y,"","startPlayingEvent",sp,nr);
    const long sm = fInt(so,"startMatchingEventId"); y += "startMatchingEventId: " + std::to_string(sm) + "\n"; evLine(y,"","startMatchingEvent",sm,nr);
    y += "rootBoneIndex: " + std::to_string(fInt(so,"rootBoneIndex")) + "\n";
    y += "otherBoneIndex: " + std::to_string(fInt(so,"otherBoneIndex")) + "\n";
    y += "anotherBoneIndex: " + std::to_string(fInt(so,"anotherBoneIndex")) + "\n";
    y += "pelvisIndex: " + std::to_string(fInt(so,"pelvisIndex")) + "\n";
    y += "mode: " + std::string(fInt(so,"mode") == 1 ? "MODE_PLAY" : "MODE_MATCH") + "\n";
    return y;
}

std::string renderModifierGenerator(const io::SchemaObject& so, const Identity& id, const NameResolver& nr) {
    std::string y = "class: hkbModifierGenerator\n";
    y += "name: " + q(fStr(so,"name")) + "\n";
    y += "userData: " + std::to_string(fInt(so,"userData")) + "\n";
    if (auto m = fPtr(so,"modifier"))  { const std::string r=refIdOf(m,id); if(!r.empty()) y += "modifier: "  + r + "\n"; }
    if (auto g = fPtr(so,"generator")) { const std::string r=refIdOf(g,id); if(!r.empty()) y += "generator: " + r + "\n"; }
    y += bindingsBlock(fPtr(so,"variableBindingSet"), nr, "");
    return y;
}

std::string modifierEvent(const io::SchemaObject& ev, const NameResolver& nr, const std::string& key);  // fwd

// hkbEvaluateExpressionModifier: `expressions: null` (the array is a data/ sidecar, linked by name),
// then bindings. (The data/<name>_expressions sidecar itself isn't emitted here yet — not gated.)
std::string renderEvaluateExpression(const io::SchemaObject& so, const Identity&, const NameResolver& nr) {
    std::string y = "class: hkbEvaluateExpressionModifier\n";
    y += "name: " + q(fStr(so,"name")) + "\n";
    y += "userData: " + std::to_string(fInt(so,"userData")) + "\n";
    y += "enable: " + boolstr(fBool(so,"enable")) + "\n";
    y += "expressions: null\n";
    y += bindingsBlock(fPtr(so,"variableBindingSet"), nr, "");
    return y;
}

// hkbExpressionData::eventMode (sbyte) — matches the decompiler's exprModeStr.
std::string exprEventMode(long v) {
    switch (v) {
        case 1:  return "EVENT_MODE_SEND_ON_TRUE";
        case 2:  return "EVENT_MODE_SEND_ON_FALSE_TO_TRUE";
        case 3:  return "EVENT_MODE_SEND_EVERY_FRAME_ONCE_TRUE";
        default: return "EVENT_MODE_SEND_ONCE";
    }
}

// data/<id>_<suffix> SIDECARS — hkbEvaluateExpressionModifier.expressions, hkbEventsFromRangeModifier.
// eventRanges, and the IK/ragdoll/keyframe modifiers' bone-index arrays are emitted as SEPARATE files
// (the modifier YAML says `<field>: null`); the runtime re-links by the naming convention <id>_<suffix>.
// Absent the sidecar the array stays null and Havok null-derefs it the moment the modifier runs (the
// hkbExpressionDataArray copy crash). One renderer per array class; empty string => nothing to emit.
std::string renderExpressionsSidecar(const io::SchemaObject& mod, const std::string& sidecarName, const NameResolver& nr) {
    const io::FieldValue* ex = fieldByName(mod, "expressions");
    const auto* arr = (ex && ex->obj) ? dynamic_cast<const io::SchemaObject*>(ex->obj.get()) : nullptr;
    if (!arr) return {};
    std::string y = "class: hkbExpressionDataArray\nname: " + q(sidecarName) + "\nexpressionsData:\n";
    if (const io::FieldValue* data = fieldByName(*arr, "expressionsData"))
        for (const auto& e : data->objs) {
            const auto* ed = dynamic_cast<const io::SchemaObject*>(e.get()); if (!ed) continue;
            y += "  - expression: " + q(fStr(*ed, "expression")) + "\n";
            const long avi = fInt(*ed, "assignmentVariableIndex");
            y += "    assignmentVariableIndex: " + std::to_string(avi) + "\n";
            varLine(y, "    ", "assignmentVariable", avi, nr);
            const long aei = fInt(*ed, "assignmentEventIndex");
            y += "    assignmentEventIndex: " + std::to_string(aei) + "\n";
            evLine(y, "    ", "assignmentEvent", aei, nr);
            y += "    eventMode: " + exprEventMode(fInt(*ed, "eventMode")) + "\n";
        }
    return y;
}
std::string renderEventRangesSidecar(const io::SchemaObject& mod, const std::string& sidecarName, const NameResolver& nr) {
    const io::FieldValue* er = fieldByName(mod, "eventRanges");
    const auto* arr = (er && er->obj) ? dynamic_cast<const io::SchemaObject*>(er->obj.get()) : nullptr;
    if (!arr) return {};
    std::string y = "class: hkbEventRangeDataArray\nname: " + q(sidecarName) + "\neventData:\n";
    if (const io::FieldValue* data = fieldByName(*arr, "eventData"))
        for (const auto& e : data->objs) {
            const auto* ed = dynamic_cast<const io::SchemaObject*>(e.get()); if (!ed) continue;
            y += "  - upperBound: " + fstr(decodeFloat(fieldByName(*ed, "upperBound")->raw)) + "\n";
            if (const io::FieldValue* evp = fieldByName(*ed, "event"); evp && evp->obj)
                if (const auto* evo = dynamic_cast<const io::SchemaObject*>(evp->obj.get()))
                    y += renderEventProperty(*evo, nr, "    ");
            y += "    eventMode: " + exprEventMode(fInt(*ed, "eventMode")) + "\n";
        }
    return y;
}
std::string renderBoneIndexSidecar(const io::SchemaObject* arr, const std::string& sidecarName) {
    if (!arr) return {};
    std::string y = "class: hkbBoneIndexArray\nname: " + q(sidecarName) + "\nboneIndices:\n";
    if (const io::FieldValue* bi = fieldByName(*arr, "boneIndices")) {
        const std::size_t k = bi->raw.size() / 2;   // int16 scalar array
        for (std::size_t i = 0; i < k; ++i) {
            std::int16_t v; std::memcpy(&v, bi->raw.data() + i * 2, 2);
            y += "  - " + std::to_string(static_cast<int>(v)) + "\n";
        }
    }
    return y;
}

// A FormId "idx:local" is a valid id STRING (and the identity keyOf reads) but NOT a valid Windows
// FILENAME — ':' is reserved. The on-disk node filename swaps ':' -> '-'; the `id:` field content keeps
// the colon. Bijective + reversible; bare ids and Nemesis "code$N" contain no colon, so unaffected.
static std::string fsId(const std::string& id) {
    std::string s = id;
    for (char& c : s) if (c == ':') c = '-';
    return s;
}

// Write every data/ sidecar a file-node modifier owns (id keys the file: data/<id>_<suffix>.yaml). Called
// right after the modifier's own YAML so the sidecar always ships with it (full emit AND per-mod delta).
void emitModifierSidecars(const io::SchemaObject& so, const std::string& id,
                          const std::filesystem::path& outDir, const NameResolver& nr) {
    namespace fs = std::filesystem;
    std::error_code ec;
    // Each sidecar is a .hky node: `id: <id>_<suffix>` then the body. (Matches DecompileNativeDelta.)
    // The FILENAME is the id + suffix (a stable #NNNN / mod$N id is filename-legal).
    auto put = [&](const std::string& suffix, const std::string& body) {
        if (body.empty()) return;
        const fs::path dir = outDir / "data";
        fs::create_directories(dir, ec);
        std::ofstream(dir / (fsId(id + suffix) + ".yaml"), std::ios::binary) << "id: " << id << suffix << "\n" << body;
    };
    const std::string cls   = so.ClassName();
    const std::string mname = fStr(so, "name");   // expressions/eventRanges `name:` = the MODIFIER's name;
    if (cls == "hkbEvaluateExpressionModifier") put("_expressions", renderExpressionsSidecar(so, mname, nr));
    else if (cls == "hkbEventsFromRangeModifier") put("_eventRanges", renderEventRangesSidecar(so, mname, nr));
    else if (cls == "hkbKeyframeBonesModifier") {   // bone-index arrays `name:` = <id>_<suffix>
        const io::FieldValue* f = fieldByName(so, "keyframedBonesList");
        put("_keyframedBonesList", renderBoneIndexSidecar(f && f->obj ? dynamic_cast<const io::SchemaObject*>(f->obj.get()) : nullptr, id + "_keyframedBonesList"));
    } else {   // IK / ragdoll modifiers own a `bones` hkbBoneIndexArray
        if (const io::FieldValue* f = fieldByName(so, "bones"); f && f->obj)
            put("_bones", renderBoneIndexSidecar(dynamic_cast<const io::SchemaObject*>(f->obj.get()), id + "_bones"));
    }
}

// 12 foot-IK gain fields, at `ind` indent.
std::string gainsFields(const io::SchemaObject& g, const std::string& ind) {
    static const char* names[] = {
        "onOffGain","groundAscendingGain","groundDescendingGain","footPlantedGain","footRaisedGain",
        "footUnlockGain","worldFromModelFeedbackGain","errorUpDownBias","alignWorldFromModelGain",
        "hipOrientationGain","maxKneeAngleDifference","ankleOrientationGain" };
    std::string y;
    for (const char* n : names) y += ind + n + ": " + fstr(fFloat(g, n)) + "\n";
    return y;
}
// hkbEventProperty as `<key>:\n<ind>id: <id>\n<ind>event: '<name>'\n<ind>payload:` at a given indent.
std::string evBlock(const io::SchemaObject& ev, const NameResolver& nr, const std::string& key, const std::string& ind) {
    const long id = fInt(ev, "id");
    std::string y = key + ":\n" + ind + "id: " + std::to_string(id) + "\n";
    const std::string n = nr.safeEvent(id); if (!n.empty()) y += ind + "event: " + q(n) + "\n";
    if (auto pl = fPtr(ev,"payload")) y += ind + "payload: " + q(payloadStr(pl)) + "\n";
    return y;
}

std::string renderRigidBodyRagdollControls(const io::SchemaObject& so, const Identity&, const NameResolver& nr) {
    std::string y = "class: hkbRigidBodyRagdollControlsModifier\n";
    y += "name: " + q(fStr(so,"name")) + "\n";
    y += "userData: " + std::to_string(fInt(so,"userData")) + "\n";
    y += "enable: " + boolstr(fBool(so,"enable")) + "\n";
    y += bindingsBlock(fPtr(so,"variableBindingSet"), nr, "");
    if (auto cd = fPtr(so,"controlData")) if (auto* c = dynamic_cast<const io::SchemaObject*>(cd.get())) {
        y += "durationToBlend: " + fstr(fFloat(*c,"durationToBlend")) + "\n";
        if (auto k = fPtr(*c,"keyFrameHierarchyControlData")) if (auto* kfh = dynamic_cast<const io::SchemaObject*>(k.get())) {
            static const char* g[] = {"hierarchyGain","velocityDamping","accelerationGain","velocityGain",
                "positionGain","positionMaxLinearVelocity","positionMaxAngularVelocity","snapGain",
                "snapMaxLinearVelocity","snapMaxAngularVelocity","snapMaxLinearDistance","snapMaxAngularDistance"};
            for (const char* n : g) y += std::string(n) + ": " + fstr(fFloat(*kfh, n)) + "\n";
        }
    }
    return y;
}

std::string renderPoweredRagdollControls(const io::SchemaObject& so, const Identity&, const NameResolver& nr) {
    std::string y = "class: hkbPoweredRagdollControlsModifier\n";
    y += "name: " + q(fStr(so,"name")) + "\n";
    y += "userData: " + std::to_string(fInt(so,"userData")) + "\n";
    y += "enable: " + boolstr(fBool(so,"enable")) + "\n";
    y += bindingsBlock(fPtr(so,"variableBindingSet"), nr, "");
    if (auto cd = fPtr(so,"controlData")) if (auto* c = dynamic_cast<const io::SchemaObject*>(cd.get())) {
        y += "maxForce: " + fstr(fFloat(*c,"maxForce")) + "\n";
        y += "tau: " + fstr(fFloat(*c,"tau")) + "\n";
        y += "damping: " + fstr(fFloat(*c,"damping")) + "\n";
        y += "proportionalRecoveryVelocity: " + fstr(fFloat(*c,"proportionalRecoveryVelocity")) + "\n";
        y += "constantRecoveryVelocity: " + fstr(fFloat(*c,"constantRecoveryVelocity")) + "\n";
    }
    if (auto w = fPtr(so,"worldFromModelModeData")) if (auto* wfm = dynamic_cast<const io::SchemaObject*>(w.get())) {
        y += "poseMatchingBone0: " + std::to_string(fInt(*wfm,"poseMatchingBone0")) + "\n";
        y += "poseMatchingBone1: " + std::to_string(fInt(*wfm,"poseMatchingBone1")) + "\n";
        y += "poseMatchingBone2: " + std::to_string(fInt(*wfm,"poseMatchingBone2")) + "\n";
        y += "worldFromModelMode: " + std::to_string(fInt(*wfm,"mode")) + "\n";
    }
    return y;
}

std::string renderFootIkControls(const io::SchemaObject& so, const Identity&, const NameResolver& nr) {
    std::string y = "class: hkbFootIkControlsModifier\n";
    y += "name: " + q(fStr(so,"name")) + "\n";
    y += "userData: " + std::to_string(fInt(so,"userData")) + "\n";
    y += "enable: " + boolstr(fBool(so,"enable")) + "\n";
    y += bindingsBlock(fPtr(so,"variableBindingSet"), nr, "");
    y += "controlData:\n  gains:\n";
    if (auto cd = fPtr(so,"controlData")) if (auto* c = dynamic_cast<const io::SchemaObject*>(cd.get()))
        if (auto g = fPtr(*c,"gains")) if (auto* gains = dynamic_cast<const io::SchemaObject*>(g.get()))
            y += gainsFields(*gains, "    ");
    const io::FieldValue* eot = fieldByName(so,"errorOutTranslation");
    y += "errorOutTranslation: " + pvecRaw(eot ? eot->raw : std::vector<std::uint8_t>{}) + "\n";
    const io::FieldValue* awg = fieldByName(so,"alignWithGroundRotation");
    y += "alignWithGroundRotation: " + pvecRaw(awg ? awg->raw : std::vector<std::uint8_t>{}) + "\n";
    const io::FieldValue* legs = fieldByName(so,"legs");
    if (legs && !legs->objs.empty()) {
        y += "legs:\n";
        for (const auto& le : legs->objs) {
            const auto* l = dynamic_cast<const io::SchemaObject*>(le.get());
            if (!l) continue;
            const io::FieldValue* gp = fieldByName(*l,"groundPosition");
            y += "  - groundPosition: " + pvecRaw(gp ? gp->raw : std::vector<std::uint8_t>{}) + "\n";
            if (auto ue = fPtr(*l,"ungroundedEvent")) if (auto* ueo = dynamic_cast<const io::SchemaObject*>(ue.get()))
                y += "    " + evBlock(*ueo, nr, "ungroundedEvent", "      ");
            y += "    verticalError: " + fstr(fFloat(*l,"verticalError")) + "\n";
            y += "    hitSomething: " + boolstr(fBool(*l,"hitSomething")) + "\n";
            y += "    isPlantedMS: " + boolstr(fBool(*l,"isPlantedMS")) + "\n";
        }
    }
    return y;
}

std::string renderLookAt(const io::SchemaObject& so, const Identity&, const NameResolver& nr) {
    std::string y = "class: BSLookAtModifier\n";
    y += "name: " + q(fStr(so,"name")) + "\n";
    y += "userData: " + std::to_string(fInt(so,"userData")) + "\n";
    y += "enable: " + boolstr(fBool(so,"enable")) + "\n";
    y += bindingsBlock(fPtr(so,"variableBindingSet"), nr, "");
    y += "lookAtTarget: " + boolstr(fBool(so,"lookAtTarget")) + "\n";
    auto boneArr = [&](const char* key) {
        const io::FieldValue* a = fieldByName(so, key);
        if (!a || a->objs.empty()) return;
        y += std::string(key) + ":\n";
        for (const auto& be : a->objs) {
            const auto* b = dynamic_cast<const io::SchemaObject*>(be.get());
            if (!b) continue;
            const io::FieldValue* fa = fieldByName(*b,"fwdAxisLS");
            y += "  - index: " + std::to_string(fInt(*b,"index")) + "\n";
            y += "    fwdAxisLS: " + pvecRaw(fa ? fa->raw : std::vector<std::uint8_t>{}) + "\n";
            y += "    limitAngleDegrees: " + fstr(fFloat(*b,"limitAngleDegrees")) + "\n";
            y += "    onGain: " + fstr(fFloat(*b,"onGain")) + "\n";
            y += "    offGain: " + fstr(fFloat(*b,"offGain")) + "\n";
            y += "    enabled: " + boolstr(fBool(*b,"enabled")) + "\n";
        }
    };
    boneArr("bones");
    boneArr("eyeBones");
    y += "limitAngleDegrees: " + fstr(fFloat(so,"limitAngleDegrees")) + "\n";
    y += "limitAngleThresholdDegrees: " + fstr(fFloat(so,"limitAngleThresholdDegrees")) + "\n";
    y += "continueLookOutsideOfLimit: " + boolstr(fBool(so,"continueLookOutsideOfLimit")) + "\n";
    y += "onGain: " + fstr(fFloat(so,"onGain")) + "\n";
    y += "offGain: " + fstr(fFloat(so,"offGain")) + "\n";
    y += "useBoneGains: " + boolstr(fBool(so,"useBoneGains")) + "\n";
    const io::FieldValue* tl = fieldByName(so,"targetLocation");
    y += "targetLocation: " + pvecRaw(tl ? tl->raw : std::vector<std::uint8_t>{}) + "\n";
    y += "targetOutsideLimits: " + boolstr(fBool(so,"targetOutsideLimits")) + "\n";
    if (auto e = fPtr(so,"targetOutOfLimitEvent")) if (auto* eo = dynamic_cast<const io::SchemaObject*>(e.get()))
        y += modifierEvent(*eo, nr, "targetOutOfLimitEvent");
    y += "lookAtCamera: " + boolstr(fBool(so,"lookAtCamera")) + "\n";
    y += "lookAtCameraX: " + fstr(fFloat(so,"lookAtCameraX")) + "\n";
    y += "lookAtCameraY: " + fstr(fFloat(so,"lookAtCameraY")) + "\n";
    y += "lookAtCameraZ: " + fstr(fFloat(so,"lookAtCameraZ")) + "\n";
    return y;
}

std::string renderEventsFromRange(const io::SchemaObject& so, const Identity&, const NameResolver& nr) {
    std::string y = "class: hkbEventsFromRangeModifier\n";
    y += "name: " + q(fStr(so,"name")) + "\n";
    y += "userData: " + std::to_string(fInt(so,"userData")) + "\n";
    y += "enable: " + boolstr(fBool(so,"enable")) + "\n";
    y += "inputValue: " + fstr(fFloat(so,"inputValue")) + "\n";
    y += "lowerBound: " + fstr(fFloat(so,"lowerBound")) + "\n";
    y += "eventRanges: null\n";   // the array is a data/ sidecar, linked by name
    y += bindingsBlock(fPtr(so,"variableBindingSet"), nr, "");
    return y;
}

std::string renderTagging(const io::SchemaObject& so, const Identity& id, const NameResolver& nr) {
    std::string y = "class: BSiStateTaggingGenerator\n";
    y += "name: " + q(fStr(so,"name")) + "\n";
    y += "userData: " + std::to_string(fInt(so,"userData")) + "\n";
    if (auto g = fPtr(so,"pDefaultGenerator")) { const std::string r=refIdOf(g,id); if(!r.empty()) y += "pDefaultGenerator: " + r + "\n"; }
    y += "iStateToSetAs: " + std::to_string(fInt(so,"iStateToSetAs")) + "\n";
    y += "iPriority: " + std::to_string(fInt(so,"iPriority")) + "\n";
    y += bindingsBlock(fPtr(so,"variableBindingSet"), nr, "");
    return y;
}

std::string renderBoneSwitch(const io::SchemaObject& so, const Identity& id, const NameResolver& nr) {
    std::string y = "class: BSBoneSwitchGenerator\n";
    y += "name: " + q(fStr(so,"name")) + "\n";
    y += "userData: " + std::to_string(fInt(so,"userData")) + "\n";
    if (auto g = fPtr(so,"pDefaultGenerator")) { const std::string r=refIdOf(g,id); if(!r.empty()) y += "pDefaultGenerator: " + r + "\n"; }
    y += bindingsBlock(fPtr(so,"variableBindingSet"), nr, "");
    y += "children:\n";
    const io::FieldValue* ch = fieldByName(so, "ChildrenA");   // ptrarray BSBoneSwitchGeneratorBoneData
    if (ch) for (const auto& ce : ch->objs) {
        const auto* c = dynamic_cast<const io::SchemaObject*>(ce.get());
        if (!c) continue;
        y += "  - pGenerator: " + refIdOf(fPtr(*c,"pGenerator"), id) + "\n";
        if (auto bw = fPtr(*c,"spBoneWeight")) y += boneWeightsBlock(bw, "    ");
        y += bindingsBlock(fPtr(*c,"variableBindingSet"), nr, "    ");
    }
    return y;
}

std::string renderCyclicBlend(const io::SchemaObject& so, const Identity& id, const NameResolver& nr) {
    std::string y = "class: BSCyclicBlendTransitionGenerator\n";
    y += "name: " + q(fStr(so,"name")) + "\n";
    y += "userData: " + std::to_string(fInt(so,"userData")) + "\n";
    if (auto g = fPtr(so,"pBlenderGenerator")) { const std::string r=refIdOf(g,id); if(!r.empty()) y += "pBlenderGenerator: " + r + "\n"; }
    if (auto e = fPtr(so,"EventToFreezeBlendValue")) if (auto* eo=dynamic_cast<const io::SchemaObject*>(e.get())) y += modifierEvent(*eo, nr, "EventToFreezeBlendValue");
    if (auto e = fPtr(so,"EventToCrossBlend"))      if (auto* eo=dynamic_cast<const io::SchemaObject*>(e.get())) y += modifierEvent(*eo, nr, "EventToCrossBlend");
    y += "fBlendParameter: " + fstr(fFloat(so,"fBlendParameter")) + "\n";
    y += "fTransitionDuration: " + fstr(fFloat(so,"fTransitionDuration")) + "\n";
    y += "eBlendCurve: " + revNum(en::BlendCurve(), fInt(so,"eBlendCurve")) + "\n";
    y += bindingsBlock(fPtr(so,"variableBindingSet"), nr, "");
    return y;
}

std::string renderManualSelector(const io::SchemaObject& so, const Identity& id, const NameResolver& nr) {
    std::string y = "class: hkbManualSelectorGenerator\n";
    y += "name: " + q(fStr(so,"name")) + "\n";
    y += "userData: " + std::to_string(fInt(so,"userData")) + "\n";
    y += "selectedGeneratorIndex: " + std::to_string(fInt(so,"selectedGeneratorIndex")) + "\n";
    y += "currentGeneratorIndex: " + std::to_string(fInt(so,"currentGeneratorIndex")) + "\n";
    y += bindingsBlock(fPtr(so,"variableBindingSet"), nr, "");
    y += "generators:\n";
    const io::FieldValue* g = fieldByName(so, "generators");
    if (g) for (const auto& ge : g->objs) y += "  - " + refIdOf(ge, id) + "\n";
    return y;
}

// Vector4/Quaternion (raw 16 bytes) -> '(x y z w)' string (behavior modifier param form).
std::string pvecRaw(const std::vector<std::uint8_t>& raw) {
    float v[4] = {0,0,0,0};
    for (int i = 0; i < 4 && raw.size() >= static_cast<std::size_t>((i+1)*4); ++i) std::memcpy(&v[i], raw.data()+i*4, 4);
    return "'(" + fstr(v[0]) + " " + fstr(v[1]) + " " + fstr(v[2]) + " " + fstr(v[3]) + ")'";
}
// hkbEventProperty in a modifier: `<key>:\n    id: <id>\n    event: '<name>'\n    payload: '<str>'`.
std::string modifierEvent(const io::SchemaObject& ev, const NameResolver& nr, const std::string& key) {
    const long id = fInt(ev, "id");
    std::string y = key + ":\n    id: " + std::to_string(id) + "\n";
    const std::string n = nr.safeEvent(id); if (!n.empty()) y += "    event: " + q(n) + "\n";
    if (auto pl = fPtr(ev, "payload")) y += "    payload: " + q(payloadStr(pl)) + "\n";
    return y;
}

// Generic modifier: fixed header (class/name/userData/enable) + bindings, then per-type fields in
// SCHEMA ORDER with value rendering (float/bool/enum/flags via renderScalar, vector4/quaternion via
// pvec, hkbEventProperty via modifierEvent). Handles the simple majority; modifiers with array/struct
// blocks (footIk legs, keyframe info, control-data) fall through their extra fields and are refined
// as their diffs surface.
std::string renderModifier(const io::SchemaObject& so, const Identity& id, const NameResolver& nr) {
    // Dedicated-handler modifiers place the bindings block at the END (after fields); the generic
    // block places it right after `enable`.
    static const std::set<std::string> kBindEnd = { "BSIsActiveModifier", "hkbEventDrivenModifier" };
    const std::string cls = so.ClassName();
    const bool bindEnd = kBindEnd.count(cls) != 0;

    std::string y = "class: " + cls + "\n";
    y += "name: " + q(fStr(so,"name")) + "\n";
    y += "userData: " + std::to_string(fInt(so,"userData")) + "\n";
    y += "enable: " + boolstr(fBool(so,"enable")) + "\n";
    if (!bindEnd) y += bindingsBlock(fPtr(so,"variableBindingSet"), nr, "");
    const auto fields = so.Fields();
    const auto& vals = so.Values();
    for (std::size_t i = 0; i < fields.size(); ++i) {
        const Field& f = *fields[i];
        if (f.name.empty() || f.name == "name" || f.name == "userData" || f.name == "enable"
            || f.name == "variableBindingSet") continue;
        if (f.ignored && !f.hkyEmit) continue;
        switch (f.kind) {
            case FieldKind::Scalar:
                y += f.name + ": " + renderScalar(f, vals[i]) + "\n";
                if (!f.eventRef.empty()) { const std::string n = nr.safeEvent(decodeInt(vals[i].raw, f.scalar)); if (!n.empty()) y += f.eventRef + ": " + q(n) + "\n"; }
                if (!f.varRef.empty())   { const std::string n = nr.safeVariable(decodeInt(vals[i].raw, f.scalar)); if (!n.empty()) y += f.varRef + ": " + q(n) + "\n"; }
                break;
            case FieldKind::String:     y += f.name + ": " + q(vals[i].str) + "\n"; break;
            case FieldKind::Vector4:
            case FieldKind::Quaternion: y += f.name + ": " + pvecRaw(vals[i].raw) + "\n"; break;
            case FieldKind::Struct:
                if (vals[i].obj) if (auto* eo = dynamic_cast<const io::SchemaObject*>(vals[i].obj.get())) {
                    if (std::string(eo->ClassName()) == "hkbEventProperty") y += modifierEvent(*eo, nr, f.name);
                }
                break;
            case FieldKind::Ptr:
                if (vals[i].obj) {
                    const auto* t = dynamic_cast<const io::SchemaObject*>(vals[i].obj.get());
                    if (t && std::string(t->ClassName()) == "hkbBoneIndexArray") break;   // emitted as a data/ sidecar, not inline
                    const std::string r = refIdOf(vals[i].obj, id); if (!r.empty()) y += f.name + ": " + r + "\n";
                }
                break;
            case FieldKind::PtrArray:
                if (!vals[i].objs.empty()) { y += f.name + ":\n"; for (const auto& e : vals[i].objs) y += "  - " + refIdOf(e, id) + "\n"; }
                break;
            default: break;   // struct arrays / other blocks — refined per modifier as diffs surface
        }
    }
    if (bindEnd) y += bindingsBlock(fPtr(so,"variableBindingSet"), nr, "");
    return y;
}

std::string renderStateInfo(const io::SchemaObject& so, const Identity& id, const NameResolver& nr) {
    std::string y = "class: hkbStateMachineStateInfo\n";
    y += "name: " + q(fStr(so,"name")) + "\n";
    y += "stateId: " + std::to_string(fInt(so,"stateId")) + "\n";
    y += "probability: " + fstr(fFloat(so,"probability")) + "\n";
    y += "enable: " + boolstr(fBool(so,"enable")) + "\n";
    if (auto g = fPtr(so,"generator")) { const std::string r=refIdOf(g,id); if(!r.empty()) y += "generator: " + r + "\n"; }
    y += notifyBlock(fPtr(so,"enterNotifyEvents"), "enterNotifyEvents", nr);
    y += notifyBlock(fPtr(so,"exitNotifyEvents"),  "exitNotifyEvents",  nr);
    y += transitionsBlock(fPtr(so,"transitions"), id, nr, "");
    y += bindingsBlock(fPtr(so,"variableBindingSet"), nr, "");
    return y;
}

std::string renderStateMachine(const io::SchemaObject& so, const Identity& id, const NameResolver& nr) {
    std::string y = "class: hkbStateMachine\n";
    y += "name: " + q(fStr(so,"name")) + "\n";
    y += "userData: " + std::to_string(fInt(so,"userData")) + "\n";
    y += "startStateId: " + std::to_string(fInt(so,"startStateId")) + "\n";
    long e0 = 0;   // eventToSendWhenStateOrTransitionChanges is an inline hkbEvent struct (stored in .obj)
    if (auto evp = fPtr(so,"eventToSendWhenStateOrTransitionChanges"))
        if (auto* evs = dynamic_cast<const io::SchemaObject*>(evp.get())) e0 = fInt(*evs,"id");
    y += "eventToSendWhenStateOrTransitionChanges: " + std::to_string(e0) + "\n";
    evLine(y, "", "eventToSendWhenStateOrTransitionChangesEvent", e0, nr);
    const long rp = fInt(so,"returnToPreviousStateEventId");       y += "returnToPreviousStateEventId: " + std::to_string(rp) + "\n";       evLine(y,"","returnToPreviousStateEvent",rp,nr);
    const long rt = fInt(so,"randomTransitionEventId");            y += "randomTransitionEventId: " + std::to_string(rt) + "\n";            evLine(y,"","randomTransitionEvent",rt,nr);
    const long th = fInt(so,"transitionToNextHigherStateEventId"); y += "transitionToNextHigherStateEventId: " + std::to_string(th) + "\n"; evLine(y,"","transitionToNextHigherStateEvent",th,nr);
    const long tl = fInt(so,"transitionToNextLowerStateEventId");  y += "transitionToNextLowerStateEventId: " + std::to_string(tl) + "\n";  evLine(y,"","transitionToNextLowerStateEvent",tl,nr);
    const long sv = fInt(so,"syncVariableIndex");                  y += "syncVariableIndex: " + std::to_string(sv) + "\n";                  varLine(y,"","syncVariable",sv,nr);
    y += "wrapAroundStateId: " + boolstr(fBool(so,"wrapAroundStateId")) + "\n";
    y += "maxSimultaneousTransitions: " + std::to_string(fInt(so,"maxSimultaneousTransitions")) + "\n";
    y += "startStateMode: " + revNum(en::StartStateMode(), fInt(so,"startStateMode")) + "\n";
    y += "selfTransitionMode: " + revNum(en::SmSelfTransitionMode(), fInt(so,"selfTransitionMode")) + "\n";
    y += bindingsBlock(fPtr(so,"variableBindingSet"), nr, "");
    y += transitionsBlock(fPtr(so,"wildcardTransitions"), id, nr, "");
    const io::FieldValue* st = fieldByName(so,"states");
    if (st) { y += "states:\n"; for (const auto& s : st->objs) { const std::string r=refIdOf(s,id); if(!r.empty()) y += "  - " + r + "\n"; } }
    return y;
}

// ── generic tagfile-XML emit (the tagfile codec) ─────────────────────────────────────────────────
std::string pad4(int n) { char b[8]; std::snprintf(b, sizeof b, "%04d", n); return b; }

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
// Render a canonical id back to a tagfile ref token: numeric -> "#" + zero-padded-4 (byte-identical to the
// old int path); new-node -> "#" + the token verbatim.
std::string tagRef(const std::string& cid) {
    return allDigits(cid) ? "#" + pad4(static_cast<int>(std::strtol(cid.c_str(), nullptr, 10))) : "#" + cid;
}

std::string refTag(const std::shared_ptr<IHavokObject>& p, const Identity& id) {
    if (!p) return "null";
    auto it = id.ids.find(p.get());
    return it == id.ids.end() ? "null" : tagRef(it->second);
}

// Tagfile float: fixed 6-decimal "%f" (the tagfile's numeric convention — distinct from the .hky
// YAML's %.9g). Vec4/quaternion render "(x y z w)" from a 16-byte raw slot.
std::string tff(float f) { char b[48]; std::snprintf(b, sizeof b, "%f", f); return b; }
// Entity-escape a string VALUE for tagfile element text (& < > — the chars that would desync an XML
// reader). Matches the real Havok tagfile; the parser's appendDecoded is the exact inverse. Expression
// conditions ("state < 10") are the ones that carry these in practice.
std::string xmlEsc(const std::string& s) {
    std::string o; o.reserve(s.size());
    for (char c : s) {
        switch (c) {
            case '&': o += "&amp;"; break;
            case '<': o += "&lt;";  break;
            case '>': o += "&gt;";  break;
            default:  o += c;       break;
        }
    }
    return o;
}
std::string tfVec4(const std::uint8_t* p) {
    float q[4] = {0,0,0,0}; std::memcpy(q, p, 16);
    return "(" + tff(q[0]) + " " + tff(q[1]) + " " + tff(q[2]) + " " + tff(q[3]) + ")";
}
// Tagfile flags render (hkbRoleAttribute.flags): 0 -> "0"; else the SET known bits in DESCENDING
// value order, then an always-present "FLAG_NONE", then the leftover (Havok-internal) bits as a
// "<!-- UNKNOWN BITS -->0xNNNN" comment. (Verified against templates/0_master.xml.)
std::string tfFlags(long v, const std::unordered_map<std::string, long>& t) {
    if (v == 0) return "0";
    std::vector<std::pair<long, std::string>> bits;
    for (const auto& [n, val] : t) if (val > 0 && (v & val) == val) bits.push_back({ val, n });
    std::sort(bits.begin(), bits.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
    long known = 0; std::string s;
    for (const auto& [val, n] : bits) { s += n + "|"; known |= val; }
    s += "FLAG_NONE|";
    char h[16]; std::snprintf(h, sizeof h, "0x%x", static_cast<unsigned int>(v & ~known));
    return s + "<!-- UNKNOWN BITS -->" + h;
}

// One object's fields -> tagfile hkparams (no <hkobject> wrapper — the caller frames it, so this
// serves both top-level named objects and inline struct blocks). `ind` is the hkparam indentation.
void tfBody(std::string& x, const std::string& ind, const io::SchemaObject& so, const Identity& id) {
    const auto  fields = so.Fields();
    const auto& vals   = so.Values();
    const std::string in2 = ind + "\t";
    for (std::size_t i = 0; i < fields.size(); ++i) {
        const Field& f = *fields[i];
        if (f.name.empty()) continue;                 // vtable / skip / pad — not tagfile params
        if (f.ignored) { x += ind + "<!-- " + f.name + " SERIALIZE_IGNORED -->\n"; continue; }
        const io::FieldValue& v = vals[i];
        const std::string open = ind + "<hkparam name=\"" + f.name + "\"";
        switch (f.kind) {
            case FieldKind::Scalar: {
                // The tagfile symbolizes a few enums the schema keeps numeric (variable-info
                // role/type/flags). Resolve them here (tagfile-only, no schema/.hky change); else fall
                // back to renderScalar (schema-marked enums + plain numbers).
                std::string sval;
                if (f.enumName.empty()) {
                    const std::string cls = so.ClassName();
                    const std::unordered_map<std::string, long>* t = nullptr; bool fl = false;
                    if      (cls == "hkbVariableInfo"  && f.name == "type")  t = &en::VariableType();
                    else if (cls == "hkbRoleAttribute" && f.name == "role")  t = &en::Role();
                    else if (cls == "hkbRoleAttribute" && f.name == "flags") { sval = tfFlags(decodeInt(v.raw, f.scalar), en::RoleFlags()); }
                    if (t) { const long iv = decodeInt(v.raw, f.scalar); sval = fl ? en::FormatFlags(iv, *t) : revNum(*t, iv); }
                }
                if (sval.empty()) sval = f.hkyOnlyEnum ? std::to_string(decodeInt(v.raw, f.scalar)) : renderScalar(f, v);
                x += open + ">" + sval + "</hkparam>\n";
                break;
            }
            case FieldKind::String:
            case FieldKind::CString: x += open + ">" + xmlEsc(v.str) + "</hkparam>\n"; break;
            case FieldKind::Ptr:     x += open + ">" + refTag(v.obj, id) + "</hkparam>\n"; break;
            case FieldKind::Vector4:
            case FieldKind::Quaternion:
                x += open + ">" + (v.raw.size() >= 16 ? tfVec4(v.raw.data()) : std::string("(0.000000 0.000000 0.000000 0.000000)")) + "</hkparam>\n";
                break;

            case FieldKind::Vec4Array: {
                const std::size_t k = v.raw.size() / 16;
                x += open + " numelements=\"" + std::to_string(k) + "\">";
                if (k == 0) { x += "</hkparam>\n"; break; }
                x += "\n";
                for (std::size_t e = 0; e < k; ++e) x += in2 + tfVec4(v.raw.data() + e * 16) + "\n";
                x += ind + "</hkparam>\n";
                break;
            }
            case FieldKind::ScalarArray: {
                const int w = schema::ScalarWidth(f.scalar);
                const std::size_t k = w > 0 ? v.raw.size() / static_cast<std::size_t>(w) : 0;
                x += open + " numelements=\"" + std::to_string(k) + "\">";
                if (k == 0) { x += "</hkparam>\n"; break; }
                x += "\n";
                for (std::size_t e = 0; e < k; ++e) {
                    if (f.scalar == Scalar::Float) { float fv; std::memcpy(&fv, v.raw.data() + e * 4, 4); x += in2 + tff(fv) + "\n"; }
                    else { std::vector<std::uint8_t> one(v.raw.begin() + e * w, v.raw.begin() + (e + 1) * w);
                           x += in2 + std::to_string(decodeInt(one, f.scalar)) + "\n"; }
                }
                x += ind + "</hkparam>\n";
                break;
            }

            case FieldKind::PtrArray:
                x += open + " numelements=\"" + std::to_string(v.objs.size()) + "\">";
                if (v.objs.empty()) { x += "</hkparam>\n"; break; }
                x += "\n" + in2;
                for (const auto& e : v.objs) x += refTag(e, id) + " ";
                x.pop_back();                          // drop the trailing space
                x += "\n" + ind + "</hkparam>\n";
                break;

            case FieldKind::StringArray:
                x += open + " numelements=\"" + std::to_string(v.strs.size()) + "\">";
                if (v.strs.empty()) { x += "</hkparam>\n"; break; }
                x += "\n";
                for (const auto& s : v.strs) x += in2 + "<hkcstring>" + xmlEsc(s) + "</hkcstring>\n";
                x += ind + "</hkparam>\n";
                break;

            case FieldKind::Struct:
                if (v.obj) if (const auto* eo = dynamic_cast<const io::SchemaObject*>(v.obj.get())) {
                    x += open + ">\n" + in2 + "<hkobject>\n";
                    tfBody(x, in2 + "\t", *eo, id);
                    x += in2 + "</hkobject>\n" + ind + "</hkparam>\n";
                }
                break;

            case FieldKind::StructArray:
                x += open + " numelements=\"" + std::to_string(v.objs.size()) + "\">";
                if (v.objs.empty()) { x += "</hkparam>\n"; break; }
                x += "\n";
                for (const auto& e : v.objs) if (const auto* eo = dynamic_cast<const io::SchemaObject*>(e.get())) {
                    x += in2 + "<hkobject>\n";
                    tfBody(x, in2 + "\t", *eo, id);
                    x += in2 + "</hkobject>\n";
                }
                x += ind + "</hkparam>\n";
                break;

            default: break;   // ScalarArray / Vec4Array / QsTransformArray / BoolArray / EmptyArray /
                              // EmptyPtr — refined against the template gate.
        }
    }
}
} // namespace

bool EmitHky(const Identity& identity, const schema::SchemaRegistry& /*reg*/,
             const std::string& outDir, std::string& err, const std::set<std::string>* deltaIds,
             std::vector<std::string>* warnings) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const NameResolver nr = buildResolver(identity);

    // Delta mode: emit ONLY the file nodes a patch touched. A patch edits inline sub-objects (the
    // category-"" nodes: transition/event arrays, conditions, clip-trigger arrays, binding sets) BY THEIR
    // OWN #NNNN, but those get no file — their corrected content lives in the OWNING node's YAML. So fold
    // each category-"" delta id up to the file node that inlines it (mirrors DecompileNativeDelta's
    // em.subOwner fold). A delta id with no file-node owner is not referenced by the merged graph — its
    // edit is moot (another change repointed its former owner); surface it rather than silently drop.
    std::set<std::string> writeIds;
    if (deltaIds) {
        std::unordered_map<std::string, const io::SchemaObject*> byId;
        for (const auto& [o, c] : identity.category)
            if (const auto* s = dynamic_cast<const io::SchemaObject*>(o)) byId[identity.ids.at(o)] = s;
        auto catStr = [&](const IHavokObject* o) -> const std::string* {
            auto c = identity.category.find(o); return c == identity.category.end() ? nullptr : &c->second;
        };

        // ONE DFS from the graph root, mirroring the typed decompiler's sink-based emit: it (a) collects
        // every REACHABLE top-level id — only reachable nodes emit; an added-but-unwired node is moot —
        // and (b) folds each reachable category-"" node (transition/event arrays, conditions, clip-trigger
        // arrays, binding sets — a Nemesis patch edits these by their OWN #NNNN, but they get no file) up
        // to the nearest enclosing FILE node, whose YAML carries the corrected inline block. Edges are
        // followed THROUGH inline (id-less) structs, so a node reachable only via a transition-info struct
        // (its effect ptr) is still seen. `owner` on the stack is the nearest enclosing file-node id.
        std::set<std::string> reachable;                 // reachable top-level ids
        std::unordered_map<std::string, std::string> subOwner;
        const IHavokObject* rootObj = nullptr;
        for (const auto& [o, c] : identity.category)
            if (std::string(o->ClassName()) == "hkRootLevelContainer") { rootObj = o; break; }

        const bool haveRoot = rootObj != nullptr;
        if (haveRoot) {
            std::vector<std::pair<const IHavokObject*, std::string>> st;
            if (auto rit = identity.ids.find(rootObj); rit != identity.ids.end()) reachable.insert(rit->second);
            st.push_back({ rootObj, std::string() });
            while (!st.empty()) {
                auto [o, owner] = st.back(); st.pop_back();
                const auto* s = dynamic_cast<const io::SchemaObject*>(o);
                if (!s) continue;
                std::string childOwner = owner;
                if (auto idIt = identity.ids.find(o); idIt != identity.ids.end()) {
                    const std::string& myId = idIt->second;
                    const std::string* c = catStr(o);
                    if (c && !c->empty()) childOwner = myId;                         // this is a file node
                    else if (!owner.empty() && !subOwner.count(myId)) subOwner[myId] = owner;  // fold to owner
                }
                for (const io::FieldValue& v : s->Values()) {
                    auto follow = [&](const std::shared_ptr<IHavokObject>& p) {
                        if (!p) return;
                        auto pit = identity.ids.find(p.get());
                        if (pit != identity.ids.end()) {                            // top-level node
                            if (!reachable.insert(pit->second).second) return;      // already visited
                        }
                        st.push_back({ p.get(), childOwner });                      // recurse (inline: no id, tree)
                    };
                    follow(v.obj); for (const auto& e : v.objs) follow(e);
                }
            }
        }

        for (const std::string& did : *deltaIds) {
            const io::SchemaObject* s = byId.count(did) ? byId[did] : nullptr;
            // Vocab symbol nodes carry the roster, not graph structure — a patch's edit to them ships via
            // data/additive.yaml (EmitAdditiveVocab), never a node file. Skip silently (no owner, no moot).
            if (s) {
                const std::string c = s->ClassName();
                if (c == "hkbBehaviorGraphStringData" || c == "hkbBehaviorGraphData" || c == "hkbVariableValueSet") continue;
            }
            const std::string* cat = s ? catStr(static_cast<const IHavokObject*>(s)) : nullptr;
            if (haveRoot && !reachable.count(did)) {       // added/edited but not wired into the merged graph
                if (warnings) warnings->push_back("delta: changed object #" + did +
                                                  " is not referenced by the merged graph — edit skipped as moot");
                continue;
            }
            if (cat && !cat->empty()) { writeIds.insert(did); continue; }   // reachable file node
            auto oit = subOwner.find(did);                                  // reachable category-"" -> owner
            if (oit != subOwner.end()) writeIds.insert(oit->second);
            else if (warnings) warnings->push_back("delta: changed object #" + did +
                                                   " has no file-node owner — edit skipped as moot");
        }
    }

    for (const auto& [obj, cat] : identity.category) {
        if (cat.empty()) continue;
        const auto* so = dynamic_cast<const io::SchemaObject*>(obj);
        if (!so) { err = "havok-model: EmitHky expects a SchemaObject graph"; return false; }

        const std::string id = identity.ids.at(obj);
        if (deltaIds && !writeIds.count(id)) continue;   // delta mode: only patched file nodes
        const std::string cls = so->ClassName();

        // Complex categories with hand-authored structure use dedicated renderers (reading the same
        // generic SchemaObject accessors — no typed-class dependency).
        if (cls == "hkbStateMachine" || cls == "hkbStateMachineStateInfo"
            || cls == "hkbBlenderGenerator" || cls == "hkbManualSelectorGenerator"
            || cls == "BSiStateTaggingGenerator" || cls == "BSBoneSwitchGenerator"
            || cls == "BSCyclicBlendTransitionGenerator" || cls == "hkbEvaluateExpressionModifier"
            || cls == "BSOffsetAnimationGenerator" || cls == "BSSynchronizedClipGenerator"
            || cls == "hkbReferencePoseGenerator" || cls == "BGSGamebryoSequenceGenerator"
            || cls == "hkbPoseMatchingGenerator" || cls == "hkbEventsFromRangeModifier"
            || cls == "BSLookAtModifier" || cls == "hkbFootIkControlsModifier"
            || cls == "hkbPoweredRagdollControlsModifier" || cls == "hkbRigidBodyRagdollControlsModifier"
            || cat == "modifiers") {
            std::string y;
            if      (cls == "hkbStateMachine")            y = renderStateMachine(*so, identity, nr);
            else if (cls == "hkbStateMachineStateInfo")   y = renderStateInfo(*so, identity, nr);
            else if (cls == "hkbBlenderGenerator")        y = renderBlender(*so, identity, nr);
            else if (cls == "hkbManualSelectorGenerator") y = renderManualSelector(*so, identity, nr);
            else if (cls == "BSiStateTaggingGenerator")   y = renderTagging(*so, identity, nr);
            else if (cls == "BSBoneSwitchGenerator")      y = renderBoneSwitch(*so, identity, nr);
            else if (cls == "BSCyclicBlendTransitionGenerator") y = renderCyclicBlend(*so, identity, nr);
            else if (cls == "hkbModifierGenerator")       y = renderModifierGenerator(*so, identity, nr);
            else if (cls == "hkbEvaluateExpressionModifier") y = renderEvaluateExpression(*so, identity, nr);
            else if (cls == "BSOffsetAnimationGenerator") y = renderOffsetAnim(*so, identity, nr);
            else if (cls == "BSSynchronizedClipGenerator") y = renderSyncClip(*so, identity, nr);
            else if (cls == "hkbReferencePoseGenerator")  y = renderReferencePose(*so, identity, nr);
            else if (cls == "BGSGamebryoSequenceGenerator") y = renderGamebryo(*so, identity, nr);
            else if (cls == "hkbPoseMatchingGenerator")   y = renderPoseMatching(*so, identity, nr);
            else if (cls == "hkbEventsFromRangeModifier") y = renderEventsFromRange(*so, identity, nr);
            else if (cls == "BSLookAtModifier")           y = renderLookAt(*so, identity, nr);
            else if (cls == "hkbFootIkControlsModifier")  y = renderFootIkControls(*so, identity, nr);
            else if (cls == "hkbPoweredRagdollControlsModifier")  y = renderPoweredRagdollControls(*so, identity, nr);
            else if (cls == "hkbRigidBodyRagdollControlsModifier") y = renderRigidBodyRagdollControls(*so, identity, nr);
            else                                          y = renderModifier(*so, identity, nr);
            const fs::path dir = fs::path(outDir) / cat;
            fs::create_directories(dir, ec);
            std::ofstream of(dir / (fsId(id) + ".yaml"), std::ios::binary);
            of << "id: " << id << "\n" << y;
            // A modifier that says `<field>: null` for an owned data array MUST ship the array as a
            // data/<id>_<suffix>.yaml sidecar, or the runtime re-link leaves it null and Havok crashes.
            emitModifierSidecars(*so, id, fs::path(outDir), nr);
            continue;
        }

        const std::vector<const Field*> fields = so->Fields();
        const std::vector<io::FieldValue>& vals = so->Values();

        // header: class / name / userData (fixed order — name before userData, unlike schema order).
        std::string y = "class: " + std::string(so->ClassName()) + "\n";
        auto findField = [&](const char* nm) -> int {
            for (std::size_t i = 0; i < fields.size(); ++i) if (fields[i]->name == nm) return static_cast<int>(i);
            return -1;
        };
        if (const int ni = findField("name");     ni >= 0) y += "name: " + q(vals[ni].str) + "\n";
        if (const int ui = findField("userData"); ui >= 0) y += "userData: " + std::to_string(decodeInt(vals[ui].raw, fields[ui]->scalar)) + "\n";
        // Pass 1 — scalar/string value fields in schema order (skip header/ignored-unless-hky/blocks).
        for (std::size_t i = 0; i < fields.size(); ++i) {
            const Field& f = *fields[i];
            if (f.name.empty() || isHeaderField(f.name)) continue;
            if (f.ignored && !f.hkyEmit) continue;
            if (f.kind == FieldKind::Scalar)
                y += f.name + ": " + renderScalar(f, vals[i]) + "\n";
            else if (f.kind == FieldKind::String)
                y += f.name + ": " + q(vals[i].str) + "\n";
        }
        // Pass 2 — inline blocks (ptr → category "" targets). Emitted AFTER the scalars, matching the
        // decompiler. Dispatch by target class (recurring wrapper shapes).
        for (std::size_t i = 0; i < fields.size(); ++i) {
            const Field& f = *fields[i];
            if (f.kind != FieldKind::Ptr || isHeaderField(f.name) || !vals[i].obj) continue;
            const auto* tgt = dynamic_cast<const io::SchemaObject*>(vals[i].obj.get());
            if (!tgt) continue;
            const std::string tcls = tgt->ClassName();
            if (tcls == "hkbClipTriggerArray") {
                const io::FieldValue* arr = fieldByName(*tgt, "triggers");   // structarray hkbClipTrigger
                if (!arr || arr->objs.empty()) continue;
                y += "triggers:\n";
                for (const auto& te : arr->objs) {
                    const auto* trg = dynamic_cast<const io::SchemaObject*>(te.get());
                    if (!trg) continue;
                    const io::FieldValue* lt = fieldByName(*trg, "localTime");
                    y += "  - localTime: " + fstr(lt ? decodeFloat(lt->raw) : 0.f) + "\n";
                    const Field* ef = nullptr; const io::FieldValue* evp = fieldByName(*trg, "event", &ef);
                    if (evp && evp->obj) {
                        const auto* evo = dynamic_cast<const io::SchemaObject*>(evp->obj.get());
                        if (evo) y += renderEventProperty(*evo, nr, "    ");
                    }
                    for (const char* bn : {"relativeToEndOfClip", "acyclic", "isAnnotation"}) {
                        const io::FieldValue* bv = fieldByName(*trg, bn);
                        y += std::string("    ") + bn + ": " + boolstr(bv && !bv->raw.empty() && bv->raw[0] != 0) + "\n";
                    }
                }
            }
        }
        // bindings block at the end (transitions/clips/generators; modifiers place it after the header —
        // handled when that category lands).
        if (const io::FieldValue* vbs = fieldByName(*so, "variableBindingSet"))
            y += bindingsBlock(vbs->obj, nr, "");

        const fs::path dir = fs::path(outDir) / cat;
        fs::create_directories(dir, ec);
        std::ofstream of(dir / (fsId(id) + ".yaml"), std::ios::binary);
        of << "id: " << id << "\n" << y;
    }
    return true;
}

namespace {
// Find a graph's hkbBehaviorGraphData + the hkbBehaviorGraphStringData it points at (the graph's OWN
// vocabulary roster). Returns {nullptr,nullptr} if the graph carries no data node.
std::pair<const io::SchemaObject*, const io::SchemaObject*> findGraphData(const Identity& id) {
    for (const auto& [obj, cat] : id.category) {
        if (std::string(obj->ClassName()) != "hkbBehaviorGraphData") continue;
        const auto* gd = dynamic_cast<const io::SchemaObject*>(obj);
        if (!gd) continue;
        const io::FieldValue* sdF = fieldByName(*gd, "stringData");
        const auto* sd = (sdF && sdF->obj) ? dynamic_cast<const io::SchemaObject*>(sdF->obj.get()) : nullptr;
        return { gd, sd };
    }
    return { nullptr, nullptr };
}
const io::SchemaObject* elemAt(const io::FieldValue* arr, std::size_t i) {
    if (!arr || i >= arr->objs.size()) return nullptr;
    return dynamic_cast<const io::SchemaObject*>(arr->objs[i].get());
}
} // namespace

bool EmitAdditiveVocab(const Identity& baseId, const Identity& mergedId,
                       const std::string& outDir, std::string& err) {
    namespace fs = std::filesystem;
    namespace en = CB::core::common::enums;
    using schema::Scalar;
    const auto [bGd, bSd] = findGraphData(baseId);
    const auto [mGd, mSd] = findGraphData(mergedId);
    (void)bGd;
    if (!mGd || !mSd) return true;   // no graph vocab to diff — nothing to add (not an error)

    auto namesOf = [](const io::SchemaObject* sd, const char* f) -> std::vector<std::string> {
        if (!sd) return {}; const io::FieldValue* v = fieldByName(*sd, f); return v ? v->strs : std::vector<std::string>{};
    };
    const std::vector<std::string> bEv = namesOf(bSd, "eventNames"), bVar = namesOf(bSd, "variableNames"), bCp = namesOf(bSd, "characterPropertyNames");
    const std::vector<std::string> mEv = namesOf(mSd, "eventNames"), mVar = namesOf(mSd, "variableNames"), mCp = namesOf(mSd, "characterPropertyNames");
    const std::set<std::string> bEvS(bEv.begin(), bEv.end()), bVarS(bVar.begin(), bVar.end()), bCpS(bCp.begin(), bCp.end());

    const io::FieldValue* varInfos = fieldByName(*mGd, "variableInfos");
    const io::FieldValue* evInfos  = fieldByName(*mGd, "eventInfos");
    const io::FieldValue* cpInfos  = fieldByName(*mGd, "characterPropertyInfos");
    const io::FieldValue* vivF     = fieldByName(*mGd, "variableInitialValues");
    const auto* viv = (vivF && vivF->obj) ? dynamic_cast<const io::SchemaObject*>(vivF->obj.get()) : nullptr;
    const io::FieldValue* wordVals = viv ? fieldByName(*viv, "wordVariableValues") : nullptr;
    const io::FieldValue* quadVals = viv ? fieldByName(*viv, "quadVariableValues") : nullptr;

    // Split for consistency with data/animations.yaml: each vocab kind is its OWN union file —
    // data/variables.yaml, data/events.yaml — legible to an author and union-merged at load. Character
    // properties stay in data/additive.yaml (rare, and carry the count-only-base guard); additive.yaml is
    // also still READ for events/variables, so pre-split bundles keep working.
    std::string yVar, yEv, yCp;
    // Added variables (name, type, initial value; quadValue for vector/quaternion).
    { bool hdr = false;
      for (std::size_t i = 0; i < mVar.size(); ++i) {
        if (bVarS.count(mVar[i])) continue;
        if (!hdr) { yVar += "variables:\n"; hdr = true; }
        yVar += "  - name: " + q(mVar[i]) + "\n";
        long type = 0; if (const auto* vi = elemAt(varInfos, i)) type = decodeInt(fieldByName(*vi, "type")->raw, Scalar::Int8);
        const std::string typeStr = revNum(en::VariableType(), type);
        yVar += "    type: " + typeStr + "\n";
        long val = 0; if (const auto* wv = elemAt(wordVals, i)) val = decodeInt(fieldByName(*wv, "value")->raw, Scalar::Int32);
        yVar += "    value: " + std::to_string(val) + "\n";
        if ((typeStr == "VARIABLE_TYPE_VECTOR4" || typeStr == "VARIABLE_TYPE_QUATERNION" || typeStr == "VARIABLE_TYPE_VECTOR3")
            && quadVals && val >= 0 && static_cast<std::size_t>(val) * 16 + 16 <= quadVals->raw.size())
            yVar += "    quadValue: " + pvecRaw(std::vector<std::uint8_t>(quadVals->raw.begin() + val * 16, quadVals->raw.begin() + val * 16 + 16)) + "\n";
      }
    }
    // Added events (name, flags).
    { bool hdr = false;
      for (std::size_t i = 0; i < mEv.size(); ++i) {
        if (bEvS.count(mEv[i])) continue;
        if (!hdr) { yEv += "events:\n"; hdr = true; }
        yEv += "  - name: " + q(mEv[i]) + "\n";
        long flags = 0; if (const auto* ei = elemAt(evInfos, i)) flags = decodeInt(fieldByName(*ei, "flags")->raw, Scalar::UInt32);
        yEv += "    flags: " + en::FormatFlags(flags, en::EventInfoFlags()) + "\n";
      }
    }
    // Added character properties (name, type, role flags).
    { bool hdr = false;
      for (std::size_t i = 0; i < mCp.size(); ++i) {
        if (bCpS.count(mCp[i])) continue;
        if (!hdr) { yCp += "characterPropertyNames:\n"; hdr = true; }
        yCp += "  - name: " + q(mCp[i]) + "\n";
        const auto* ci = elemAt(cpInfos, i);
        long type = ci ? decodeInt(fieldByName(*ci, "type")->raw, Scalar::Int8) : 0;
        yCp += "    type: " + revNum(en::VariableType(), type) + "\n";
        long flags = 0;
        if (ci) if (const io::FieldValue* roleF = fieldByName(*ci, "role"); roleF && roleF->obj)
            if (const auto* role = dynamic_cast<const io::SchemaObject*>(roleF->obj.get()))
                flags = decodeInt(fieldByName(*role, "flags")->raw, Scalar::Int16);
        yCp += "    flags: " + en::FormatFlags(flags, en::RoleFlags()) + "\n";
      }
    }

    if (yVar.empty() && yEv.empty() && yCp.empty()) return true;   // no added vocabulary
    const fs::path dataDir = fs::path(outDir) / "data";
    std::error_code ec; fs::create_directories(dataDir, ec);
    auto writeVocab = [&](const char* file, const std::string& body) -> bool {
        if (body.empty()) return true;
        std::ofstream of(dataDir / file, std::ios::binary);
        of << body;
        if (!of) { err = std::string("EmitAdditiveVocab: cannot write ") + file; return false; }
        return true;
    };
    return writeVocab("variables.yaml", yVar) && writeVocab("events.yaml", yEv) && writeVocab("additive.yaml", yCp);
}

namespace {
// The graph's hkbBehaviorGraph node (name / variableMode / rootGenerator) for the behavior.yaml header.
const io::SchemaObject* findBehaviorGraph(const Identity& id) {
    for (const auto& [obj, cat] : id.category)
        if (std::string(obj->ClassName()) == "hkbBehaviorGraph")
            return dynamic_cast<const io::SchemaObject*>(obj);
    return nullptr;
}

// The FULL graph roster (every variable/event/characterProperty), NOT the base-vs-merged diff that
// EmitAdditiveVocab emits. Byte-matches the typed emitGraphData (BehaviorDecompiler.cpp) so the schema
// decompiler reproduces data/graphdata.yaml exactly. Reads the same fields EmitAdditiveVocab does, minus
// the "skip if already in base" filter.
std::string emitGraphDataFull(const io::SchemaObject* gd, const io::SchemaObject* sd) {
    namespace en = CB::core::common::enums;
    using schema::Scalar;
    auto namesOf = [](const io::SchemaObject* s, const char* f) -> std::vector<std::string> {
        if (!s) return {}; const io::FieldValue* v = fieldByName(*s, f); return v ? v->strs : std::vector<std::string>{};
    };
    const auto vars   = namesOf(sd, "variableNames");
    const auto events = namesOf(sd, "eventNames");
    const auto cps    = namesOf(sd, "characterPropertyNames");
    const io::FieldValue* varInfos = gd ? fieldByName(*gd, "variableInfos") : nullptr;
    const io::FieldValue* evInfos  = gd ? fieldByName(*gd, "eventInfos") : nullptr;
    const io::FieldValue* cpInfos  = gd ? fieldByName(*gd, "characterPropertyInfos") : nullptr;
    const io::FieldValue* vivF     = gd ? fieldByName(*gd, "variableInitialValues") : nullptr;
    const auto* viv = (vivF && vivF->obj) ? dynamic_cast<const io::SchemaObject*>(vivF->obj.get()) : nullptr;
    const io::FieldValue* wordVals = viv ? fieldByName(*viv, "wordVariableValues") : nullptr;
    const io::FieldValue* quadVals = viv ? fieldByName(*viv, "quadVariableValues") : nullptr;

    std::string y;
    if (vars.empty()) {
        y += "variables: []\n\n";
    } else {
        y += "variables:\n";
        for (std::size_t i = 0; i < vars.size(); ++i) {
            y += "  - name: " + q(vars[i]) + "\n";
            long type = 0; if (const auto* vi = elemAt(varInfos, i)) type = decodeInt(fieldByName(*vi, "type")->raw, Scalar::Int8);
            const std::string typeStr = revNum(en::VariableType(), type);
            y += "    type: " + typeStr + "\n";
            long val = 0; if (const auto* wv = elemAt(wordVals, i)) val = decodeInt(fieldByName(*wv, "value")->raw, Scalar::Int32);
            y += "    value: " + std::to_string(val) + "\n";
            if ((typeStr == "VARIABLE_TYPE_VECTOR4" || typeStr == "VARIABLE_TYPE_QUATERNION" || typeStr == "VARIABLE_TYPE_VECTOR3")
                && quadVals && val >= 0 && static_cast<std::size_t>(val) * 16 + 16 <= quadVals->raw.size())
                y += "    quadValue: " + pvecRaw(std::vector<std::uint8_t>(quadVals->raw.begin() + val * 16, quadVals->raw.begin() + val * 16 + 16)) + "\n";
        }
        y += "\n";
    }
    if (events.empty()) {
        y += "events: []\n";
    } else {
        y += "events:\n";
        for (std::size_t i = 0; i < events.size(); ++i) {
            y += "  - name: " + q(events[i]) + "\n";
            long flags = 0; if (const auto* ei = elemAt(evInfos, i)) flags = decodeInt(fieldByName(*ei, "flags")->raw, Scalar::UInt32);
            y += "    flags: " + en::FormatFlags(flags, en::EventInfoFlags()) + "\n";
        }
    }
    if (!cps.empty()) {
        y += "\ncharacterPropertyNames:\n";
        for (std::size_t i = 0; i < cps.size(); ++i) {
            y += "  - name: " + q(cps[i]) + "\n";
            const auto* ci = elemAt(cpInfos, i);
            long type = ci ? decodeInt(fieldByName(*ci, "type")->raw, Scalar::Int8) : 0;
            y += "    type: " + revNum(en::VariableType(), type) + "\n";
            long flags = 0;
            if (ci) if (const io::FieldValue* roleF = fieldByName(*ci, "role"); roleF && roleF->obj)
                if (const auto* role = dynamic_cast<const io::SchemaObject*>(roleF->obj.get()))
                    flags = decodeInt(fieldByName(*role, "flags")->raw, Scalar::Int16);
            y += "    flags: " + en::FormatFlags(flags, en::RoleFlags()) + "\n";
        }
    }
    return y;
}
} // namespace

// Whole-graph scaffolding EmitHky (node-only) omits: behavior.yaml (packfile + behavior header) and
// data/graphdata.yaml (full roster). Together with EmitHky's node files this reproduces the typed
// DecompileBehaviorTree tree byte-for-byte — the schema decompiler's full-base output. Written only for a
// FULL decompile (not a per-mod delta, which carries only touched nodes + added vocab via EmitAdditiveVocab).
bool EmitFullBaseScaffolding(const Identity& identity, const std::string& outDir, std::string& err) {
    namespace fs = std::filesystem;
    namespace en = CB::core::common::enums;
    std::error_code ec;
    // Ensure outDir exists. EmitHky (run before this) only creates a dir per EMITTED node, so a
    // near-empty graph — e.g. the creature *_lod behaviors, an hkbBehaviorGraph shell with no generator
    // tree / graphdata (the stripped "far from player" logic) — leaves outDir absent, and the
    // behavior.yaml write below then fails to open. Create it here so the minimal unit still writes.
    fs::create_directories(outDir, ec);
    const auto* bg = findBehaviorGraph(identity);
    const auto [gd, sd] = findGraphData(identity);

    std::string h;
    h += "packfile:\n  classversion: 8\n  contentsversion: \"hk_2010.2.0-r1\"\n\n";
    h += "behavior:\n";
    h += "  name: " + q(bg ? fStr(*bg, "name") : std::string()) + "\n";
    h += "  variableMode: " + revNum(en::VariableMode(), bg ? fInt(*bg, "variableMode") : 0) + "\n";
    if (bg) if (const io::FieldValue* rg = fieldByName(*bg, "rootGenerator"); rg && rg->obj)
        if (auto it = identity.ids.find(rg->obj.get()); it != identity.ids.end())
            h += "  rootGenerator: " + it->second + "\n";
    h += "  data: graphdata\n";
    { std::ofstream of(fs::path(outDir) / "behavior.yaml", std::ios::binary); of << h;
      if (!of) { err = "EmitFullBaseScaffolding: cannot write behavior.yaml"; return false; } }

    const fs::path dataDir = fs::path(outDir) / "data";
    fs::create_directories(dataDir, ec);
    { std::ofstream of(dataDir / "graphdata.yaml", std::ios::binary); of << emitGraphDataFull(gd, sd);
      if (!of) { err = "EmitFullBaseScaffolding: cannot write graphdata.yaml"; return false; } }
    return true;
}

bool DecompileBehaviorSchema(const std::vector<std::uint8_t>& bytes, const std::string& xmlText,
                             const schema::SchemaRegistry& reg, const std::string& outDir, std::string& err,
                             int selfIndex) {
    try {
        PackFileDeserializer des;
        des.ObjectFactory = io::MakeSchemaFactory(reg);
        BinaryReaderEx br(false, true, bytes);
        des.Deserialize(br);
        // xmlText = the matching vanilla tagfile → oracle #NNNN (aligns with the per-mod deltas that
        // reference those ids); empty → encounter-order (self-consistent round-trip).
        Identity ident = AssignIdentity(des, reg, xmlText);
        // FormId identity (selfIndex >= 0): every node here is owned by THIS bundle at `selfIndex` — 0
        // for the base master (Skyrim's own nodes = self = index 0). Prefix each id -> "idx:local". Because ids AND refs
        // (idRef, refTag, rootGenerator) all read from identity.ids, this makes both carry the FormId
        // form; the loader treats them as opaque keys, so compiled graphdata is byte-unchanged (17/17).
        if (selfIndex >= 0) {
            for (auto& [obj, id] : ident.ids) {
                (void)obj;
                char* end = nullptr;
                const unsigned long v = std::strtoul(id.c_str(), &end, 10);
                if (end && *end == '\0')   // numeric id -> guard the 16-bit local (hkbNode::id is ushort)
                    if (const auto f = CB::core::codec::formid::make(static_cast<std::uint32_t>(selfIndex),
                                                              static_cast<std::uint32_t>(v)))
                        id = CB::core::codec::formid::format(*f);
                // non-numeric (a secondary identity, e.g. Class:@animName) or a >16-bit id: left as-is
            }
        }
        if (!EmitHky(ident, reg, outDir, err)) return false;                 // node files
        if (!EmitFullBaseScaffolding(ident, outDir, err)) return false;      // behavior.yaml + graphdata
        return true;
    } catch (const std::exception& e) {
        err = std::string("DecompileBehaviorSchema: ") + e.what();
        return false;
    }
}

namespace {
// Schema peer of DeltaDeriver's LoadedGraph — per-SchemaObject (class,name) + owner map for the loose-
// derive name-identity match. Named referenceable nodes only; nameless inline objects (arrays, binding
// sets, conditions) are folded by EmitHky and excluded here (they never match/emit as files).
struct SGraph {
    std::vector<const void*>                                              order;   // named objs, read order
    std::unordered_map<const void*, std::pair<std::string, std::string>>  meta;    // obj -> (class,name)
    std::unordered_map<const void*, const void*>                          owner;   // obj -> owning node
    std::unordered_map<std::string, int>                                  nameCount;
};

void sLoadGraphMeta(PackFileDeserializer& des, SGraph& g) {
    // Node->child refs for the owner map. MIRROR DeltaDeriver's typed loadGraphMeta EXACTLY — only the
    // specific generator/state parent edges, NOT every object field. A blanket all-fields walk attributes
    // nodes reached via inline structs (binding sets, transition/trigger arrays) wrong, shifting owner-
    // qualification so a cross-class same-name pair (e.g. an SM state vs a pose-matching generator) can
    // collapse to one name-identity — the 15-collision bug. Recorded edges (child -> referrer):
    //   hkbStateMachine.states[i]                -> the SM
    //   hkbStateMachineStateInfo.generator       -> the state
    //   hkb{Blender,PoseMatching}Generator.children[i].generator -> the blender
    //   hkbManualSelectorGenerator.generators[i] -> the selector
    //   hkbModifierGenerator.{generator,modifier}-> the modGen
    std::unordered_map<const void*, std::vector<const void*>> refs;
    auto ptr = [](const io::FieldValue* v) -> const void* { return (v && v->obj) ? v->obj.get() : nullptr; };
    for (const auto& [off, obj] : des.DeserializedObjects()) {
        auto* so = dynamic_cast<io::SchemaObject*>(obj.get());
        if (!so) continue;
        const std::string cls = so->ClassName();
        if (cls == "hkbStateMachine") {
            if (const io::FieldValue* st = fieldByName(*so, "states"))
                for (const auto& s : st->objs) if (s) refs[s.get()].push_back(so);
        } else if (cls == "hkbStateMachineStateInfo") {
            if (const void* g2 = ptr(fieldByName(*so, "generator"))) refs[g2].push_back(so);
        } else if (cls == "hkbBlenderGenerator" || cls == "hkbPoseMatchingGenerator") {
            if (const io::FieldValue* ch = fieldByName(*so, "children"))
                for (const auto& c : ch->objs)
                    if (auto* cc = dynamic_cast<const io::SchemaObject*>(c.get()))
                        if (const void* g2 = ptr(fieldByName(*cc, "generator"))) refs[g2].push_back(so);
        } else if (cls == "hkbManualSelectorGenerator") {
            if (const io::FieldValue* gs = fieldByName(*so, "generators"))
                for (const auto& gg : gs->objs) if (gg) refs[gg.get()].push_back(so);
        } else if (cls == "hkbModifierGenerator") {
            if (const void* g2 = ptr(fieldByName(*so, "generator"))) refs[g2].push_back(so);
            if (const void* m2 = ptr(fieldByName(*so, "modifier")))  refs[m2].push_back(so);
        }
    }
    for (const auto& [off, obj] : des.DeserializedObjects()) {
        auto* so = dynamic_cast<io::SchemaObject*>(obj.get());
        if (!so) continue;
        const std::string cls = so->ClassName();
        std::string nm = fStr(*so, "name");
        if (nm.empty()) {
            if (cls == "hkbBehaviorGraph" || cls == "hkbBehaviorGraphData" ||
                cls == "hkbBehaviorGraphStringData" || cls == "hkbVariableValueSet")
                nm = "$" + cls;   // referenceable singletons need a stable identity
            else
                continue;         // nameless inline object — folded by EmitHky, not a match target
        }
        g.order.push_back(so);
        g.meta[so] = { cls, nm };
        if (nm[0] != '$') g.nameCount[nm]++;
    }
    for (const auto& [child, rs] : refs) {
        const void* best = nullptr;
        for (const void* rf : rs) {
            if (!g.meta.count(rf)) continue;
            if (!best || g.meta.at(rf) < g.meta.at(best)) best = rf;
        }
        if (best) g.owner[child] = best;
    }
}

// Identity = bare name when unambiguous in BOTH graphs, else owner-qualified ("<ownerId>~<name>"),
// then made globally unique. Verbatim port of DeltaDeriver::assignNameIds over SGraph.
void sAssignNameIds(const SGraph& g, const std::set<std::string>& ambiguous,
                    std::unordered_map<const void*, std::string>& ids) {
    std::unordered_map<const void*, std::string> ident;
    std::function<std::string(const void*)> identOf = [&](const void* o) -> std::string {
        auto mi = g.meta.find(o); if (mi == g.meta.end()) return {};
        if (auto it = ident.find(o); it != ident.end()) return it->second;
        const std::string& nm = mi->second.second;
        std::string result = nm;
        if (nm[0] != '$' && ambiguous.count(nm)) {
            ident[o] = nm;   // cycle guard
            auto oit = g.owner.find(o);
            const std::string op = (oit != g.owner.end()) ? identOf(oit->second) : std::string();
            if (!op.empty()) result = op + "~" + nm;
        }
        ident[o] = result; return result;
    };
    std::unordered_map<std::string, std::vector<const void*>> groups;
    for (const void* o : g.order) groups[identOf(o)].push_back(o);
    for (auto& [base, objs] : groups) {
        if (objs.size() > 1)
            std::stable_sort(objs.begin(), objs.end(), [&](const void* a, const void* b) {
                const auto& ma = g.meta.at(a); const auto& mb = g.meta.at(b);
                if (ma.first != mb.first) return ma.first < mb.first;
                auto oa = g.owner.find(a), ob = g.owner.find(b);
                std::string ka = (oa != g.owner.end()) ? identOf(oa->second) : std::string();
                std::string kb = (ob != g.owner.end()) ? identOf(ob->second) : std::string();
                return ka < kb;
            });
        for (std::size_t i = 0; i < objs.size(); ++i)
            ids[objs[i]] = (i == 0) ? base : base + "~" + std::to_string(i);
    }
}
} // namespace

LooseDeriveResult DeriveLooseBehaviorDeltaSchema(const std::vector<std::uint8_t>& vanBytes,
                                                 const std::vector<std::uint8_t>& modBytes,
                                                 const std::string& modCode,
                                                 const schema::SchemaRegistry& reg,
                                                 const std::string& outDeltaDir) {
    namespace fs = std::filesystem;
    LooseDeriveResult r;
    try {
        PackFileDeserializer vdes, mdes;
        vdes.ObjectFactory = io::MakeSchemaFactory(reg);
        mdes.ObjectFactory = io::MakeSchemaFactory(reg);
        { BinaryReaderEx br(false, true, vanBytes); vdes.Deserialize(br); }
        { BinaryReaderEx br(false, true, modBytes); mdes.Deserialize(br); }

        SGraph vg, mg; sLoadGraphMeta(vdes, vg); sLoadGraphMeta(mdes, mg);
        std::set<std::string> ambiguous;
        for (const auto& [k, n] : vg.nameCount) if (n > 1) ambiguous.insert(k);
        for (const auto& [k, n] : mg.nameCount) if (n > 1) ambiguous.insert(k);
        std::unordered_map<const void*, std::string> vIdent, mIdent;
        sAssignNameIds(vg, ambiguous, vIdent);
        sAssignNameIds(mg, ambiguous, mIdent);

        // Vanilla schema READ-ORDER ids — identical to what BuildBaseBundle's DecompileBehaviorSchema
        // assigns for the shipped base, so matched mod overrides land on the base's ids. Capture the raw
        // NUMERIC vanilla id per name-identity BEFORE the FormId transform (identToNum keys the matcher).
        Identity vId = AssignIdentity(vdes, reg, "");
        std::unordered_map<std::string, std::string> identToNum;
        for (const auto& [obj, id] : vId.ids)
            if (auto it = vIdent.find(obj); it != vIdent.end()) identToNum[it->second] = id;

        // FormId minting (index convention: self=0, skyrim=1 — see FormId.h). A loose-derive bundle sees
        // only vanilla (precompiled: no foreign mod codes), so it masters SKYRIM ONLY -> master table
        // [self(0), skyrim(1)]. So: an edit of a vanilla node -> "0001x<local>" (index 1 = Skyrim, the
        // origin), an own new node -> "0000x<seq>" (index 0 = self). Matches what the loader's
        // BuildMasterTable(stem, {}, haveSkyrim) resolves. (modCode is retired — the FormId's self index
        // replaces the "<code>$N" string namespace.)
        (void)modCode;
        auto toNumericFormId = [](const std::string& decId, std::uint16_t idx) -> std::string {
            char* end = nullptr;
            const unsigned long v = std::strtoul(decId.c_str(), &end, 10);
            if (end && *end == '\0')
                if (const auto f = CB::core::codec::formid::make(idx, static_cast<std::uint32_t>(v)))
                    return CB::core::codec::formid::format(*f);
            return decId;   // non-numeric (a secondary identity) or >16-bit: leave as-is
        };
        // Vanilla side = Skyrim (index 1, the origin) — every id -> "0001x<local>" (matched-unchanged diff away).
        for (auto& [obj, id] : vId.ids) { (void)obj; id = toNumericFormId(id, CB::core::codec::formid::BASE_GAME_INDEX); }

        Identity mId = AssignIdentity(mdes, reg, "");   // category for all; ids overridden for named nodes
        int newSeq = 0;
        for (const void* o : mg.order) {
            auto* obj = static_cast<IHavokObject*>(const_cast<void*>(o));
            if (auto it = identToNum.find(mIdent[o]); it != identToNum.end()) {
                mId.ids[obj] = toNumericFormId(it->second, CB::core::codec::formid::BASE_GAME_INDEX);   // edit of vanilla (origin = Skyrim)
                ++r.matched;
            } else {
                if (const auto f = CB::core::codec::formid::make(CB::core::codec::formid::SELF_INDEX, static_cast<std::uint32_t>(++newSeq)))
                    mId.ids[obj] = CB::core::codec::formid::format(*f);            // own new node (self=0)
                ++r.added;
            }
        }

        // Emit both trees, diff by relative path (same id => same filename => same node), copy each NEW or
        // CHANGED mod node into the delta (mirrors DeltaDeriver's fs diff; emitter-agnostic).
        const fs::path work = fs::temp_directory_path() /
            ("schderive_" + std::to_string(std::hash<std::string>{}(outDeltaDir) & 0xffffffffu));
        const fs::path vt = work / "van", mt = work / "mod";
        std::error_code ec; fs::remove_all(work, ec); fs::create_directories(vt, ec); fs::create_directories(mt, ec);
        std::string e;
        if (!EmitHky(vId, reg, vt.string(), e)) { r.error = "van emit: " + e; fs::remove_all(work, ec); return r; }
        EmitFullBaseScaffolding(vId, vt.string(), e);
        if (!EmitHky(mId, reg, mt.string(), e)) { r.error = "mod emit: " + e; fs::remove_all(work, ec); return r; }
        EmitFullBaseScaffolding(mId, mt.string(), e);

        auto readAll = [](const fs::path& p) { std::ifstream f(p, std::ios::binary); return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>()); };
        int copyFail = 0;
        for (fs::recursive_directory_iterator it(mt, ec), end; !ec && it != end; it.increment(ec)) {
            if (!it->is_regular_file()) continue;
            const fs::path rel = fs::relative(it->path(), mt);
            if (rel.filename() == "behavior.yaml") continue;   // graph header comes from the base
            const fs::path vp = vt / rel;
            const bool isNew = !fs::exists(vp, ec);
            if (!isNew && readAll(it->path()) == readAll(vp)) continue;   // unchanged -> base serves it
            const fs::path dst = fs::path(outDeltaDir) / rel;
            std::error_code ce; fs::create_directories(dst.parent_path(), ce);
            fs::copy_file(it->path(), dst, fs::copy_options::overwrite_existing, ce);
            if (ce) ++copyFail; else if (isNew) ++r.newNodes; else ++r.changedNodes;
        }
        if (copyFail) { r.error = "failed to write " + std::to_string(copyFail) + " delta node file(s)"; fs::remove_all(work, ec); return r; }
        for (fs::recursive_directory_iterator it(vt, ec), end; !ec && it != end; it.increment(ec))
            if (it->is_regular_file() && !fs::exists(mt / fs::relative(it->path(), vt), ec)) ++r.removedFromBase;
        fs::remove_all(work, ec);
        r.ok = true; return r;
    } catch (const std::exception& e) {
        r.error = std::string("DeriveLooseBehaviorDeltaSchema: ") + e.what();
        return r;
    }
}

namespace {
/// @note Reflective emit, MEASUREMENT stage: every schema field in schema order, rendered by FieldKind + schema
///       annotations alone. File nodes (non-empty category) render as their id; inline objects nest as maps.
///       Lossless elisions: a nested object states `class:` only when it differs from the field's declared class
///       (a subclass); a null pointer or an empty array is omitted (absent == null / empty on parse); a `boneref`
///       of -1 ("no bone") is omitted (absent == -1).
struct ReflectCtx {
    const Identity&                 id;
    const NameResolver&             nr;
    const schema::SchemaRegistry&   reg;
    const std::vector<std::string>* bones;   ///< the graph's rig, in rig order; null = unknown (indices stay numeric)
    const std::unordered_map<const IHavokObject*, std::string>* cat;   ///< effective category (file nodes + shared/)
    std::uint16_t                   keyOwner = CB::core::codec::formid::SELF_INDEX;   ///< master index of positional sub-record keys
    std::vector<std::pair<std::string, std::string>>* sink = nullptr;   ///< node-level rosters -> (field path, entries)
    std::string                     path;                            ///< dotted field path from the node to this object
};

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
    return fstr(f);
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
    if (c.bones && idx >= 0 && idx < static_cast<std::int64_t>(c.bones->size())) return q((*c.bones)[static_cast<std::size_t>(idx)]);
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
        if (vals[k] != dflt) body += "  " + q((*c.bones)[k]) + ": " + vals[k] + "\n";
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
                     const std::string& key, const std::string& declared) {
    if (objs.empty()) return {};
    std::string y = key + ":\n";
    for (const auto& p : objs) {
        if (!p) { y += "  - null\n"; continue; }
        if (isFileNode(p.get(), c)) { y += "  - " + refIdOf(p, c.id) + "\n"; continue; }
        const auto* so = dynamic_cast<const io::SchemaObject*>(p.get());
        if (!so) { y += "  - '?'\n"; continue; }
        const std::string body = reflectObject(*so, inlCtx(c), declared);
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
const io::SchemaObject* ptrObj(const io::SchemaObject& so, const char* field) {
    const io::FieldValue* v = fieldByName(so, field);
    return v ? dynamic_cast<const io::SchemaObject*>(v->obj.get()) : nullptr;
}
const std::vector<std::shared_ptr<IHavokObject>>* objsOf(const io::SchemaObject& so, const char* field) {
    const io::FieldValue* v = fieldByName(so, field);
    return v ? &v->objs : nullptr;
}
const std::vector<std::string>* strsOf(const io::SchemaObject* so, const char* field) {
    const io::FieldValue* v = so ? fieldByName(*so, field) : nullptr;
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
    const auto* sd = ptrObj(owner, "stringData");
    const auto* vs = ptrObj(owner, r.values);
    if (!sd || !vs) return false;
    const auto* names = strsOf(sd, r.names);
    const auto* infos = objsOf(owner, r.infos);
    const auto* words = objsOf(*vs, "wordVariableValues");
    const io::FieldValue* quads = fieldByName(*vs, "quadVariableValues");
    const auto* variants = objsOf(*vs, "variantVariableValues");
    if (!names || !infos || !words || !quads || !variants) return false;
    if (names->size() != infos->size() || names->size() != words->size() || (!lenient && !uniqueNames(*names))) return false;
    std::size_t q = 0, p = 0;
    for (std::size_t i = 0; i < infos->size(); ++i) {
        const auto* info = dynamic_cast<const io::SchemaObject*>((*infos)[i].get());
        const auto* word = dynamic_cast<const io::SchemaObject*>((*words)[i].get());
        if (!info || !word) return false;
        const long t = fInt(*info, "type");
        if (isPointerType(t)) { if (fInt(*word, "value") != static_cast<long>(p)) return false; ++p; }
        else if (isQuadType(t)) { if (fInt(*word, "value") != static_cast<long>(q)) return false; ++q; }
    }
    return q * 16 == quads->raw.size() && p == variants->size();
}

/// @brief `lenient` (derive's MOD side only) allows duplicate names: a mod that renames an entry onto a name the roster
///        already has (Precision renames vanilla's ' iState_NPCSneaking' onto 'iState_NPCSneaking') still diffs by
///        name — names are identity and rosters only union, so the rename drops out (first occurrence wins).
bool vocabRenderable(const io::SchemaObject& so, bool lenient = false) {
    if (so.Schema()->vocab == "character") return valuedRenderable(so, kCharacterProperties, lenient);
    if (so.Schema()->vocab != "graph" || !valuedRenderable(so, kGraphVariables, lenient)) return false;
    const auto* sd = ptrObj(so, "stringData");
    const auto* en = strsOf(sd, "eventNames");
    const auto* cn = strsOf(sd, "characterPropertyNames");
    const auto* an = strsOf(sd, "attributeNames");
    const auto* ei = objsOf(so, "eventInfos");
    const auto* ci = objsOf(so, "characterPropertyInfos");
    const io::FieldValue* ad = fieldByName(so, "attributeDefaults");
    if (!en || !cn || !an || !ei || !ci || !ad) return false;
    if (en->size() != ei->size() || cn->size() != ci->size() || an->size() * 4 != ad->raw.size()) return false;
    if (!objsOf(so, "wordMinVariableValues")->empty() || !objsOf(so, "wordMaxVariableValues")->empty()) return false;
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
    const long t = fInt(info, "type");
    std::vector<std::pair<std::string, std::string>> kv{ { "type", enumText("VariableType", t, false) } };
    if (const auto* role = ptrObj(info, "role")) {
        const long r = fInt(*role, "role");
        const long fl = static_cast<long>(static_cast<std::uint16_t>(fInt(*role, "flags")));
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
        const io::FieldValue* wv = fieldByName(*word, "value");
        std::string val;
        if (quad) val = rvec(std::vector<std::uint8_t>(quad, quad + 16));
        else if (t == 4 && wv) val = rfloat(wv->raw);
        else {
            const long w = fInt(*word, "value");
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
    const auto* infos = objsOf(owner, r.infos);
    return infos && pos < infos->size() ? dynamic_cast<const io::SchemaObject*>((*infos)[pos].get()) : nullptr;
}
std::shared_ptr<IHavokObject> valuedPointer(const io::SchemaObject& owner, const ValuedRoster& r, std::size_t pos) {
    const auto* info = valuedInfo(owner, r, pos);
    const auto* vs = ptrObj(owner, r.values);
    if (!info || !vs || !isPointerType(fInt(*info, "type"))) return nullptr;
    const auto& words = *objsOf(*vs, "wordVariableValues");
    const auto& variants = *objsOf(*vs, "variantVariableValues");
    const auto* word = pos < words.size() ? dynamic_cast<const io::SchemaObject*>(words[pos].get()) : nullptr;
    const long vi = word ? fInt(*word, "value") : -1;
    return vi >= 0 && static_cast<std::size_t>(vi) < variants.size() ? variants[static_cast<std::size_t>(vi)] : nullptr;
}

/// @brief Every record of the vocabulary, per roster, in roster order: (roster key, name, record text).
void valuedRecords(const io::SchemaObject& owner, const ValuedRoster& r, bool full, const ReflectCtx& c,
                   std::vector<std::tuple<std::string, std::string, std::string>>& out) {
    const auto* vs = ptrObj(owner, r.values);
    const auto& names = *strsOf(ptrObj(owner, "stringData"), r.names);
    const auto& infos = *objsOf(owner, r.infos);
    const auto& words = *objsOf(*vs, "wordVariableValues");
    const auto& quads = fieldByName(*vs, "quadVariableValues")->raw;
    const auto& variants = *objsOf(*vs, "variantVariableValues");
    std::size_t q = 0, p = 0;
    for (std::size_t i = 0; i < names.size(); ++i) {
        const auto& info = *dynamic_cast<const io::SchemaObject*>(infos[i].get());
        const long t = fInt(info, "type");
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
    const auto* sd = ptrObj(gd, "stringData");
    const auto& cn = *strsOf(sd, "characterPropertyNames");
    const auto& ci = *objsOf(gd, "characterPropertyInfos");
    for (std::size_t i = 0; i < cn.size(); ++i)
        out.emplace_back("characterProperties", cn[i], infoRecord(*dynamic_cast<const io::SchemaObject*>(ci[i].get()), nullptr, nullptr, nullptr, full, c));
    const auto& enm = *strsOf(sd, "eventNames");
    const auto& ei = *objsOf(gd, "eventInfos");
    for (std::size_t i = 0; i < enm.size(); ++i) {
        const long fl = static_cast<long>(static_cast<std::uint32_t>(fInt(*dynamic_cast<const io::SchemaObject*>(ei[i].get()), "flags")));
        out.emplace_back("events", enm[i], fl || full ? "{flags: " + enumText("EventInfoFlags", fl, true) + "}" : std::string("{}"));
    }
    const auto& an = *strsOf(sd, "attributeNames");
    const auto& ad = fieldByName(gd, "attributeDefaults")->raw;
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
    return rec.find('\n') == std::string::npos ? "  " + q(name) + ": " + rec + "\n"
                                               : "  " + q(name) + ":\n" + indentLines(rec, "    ", "    ");
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
    return fInt(so, cond.substr(0, eq).c_str()) == std::strtol(cond.c_str() + eq + 1, nullptr, 10);
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
std::string mintedKey(std::size_t k, std::uint16_t owner = CB::core::codec::formid::SELF_INDEX) {
    const auto f = CB::core::codec::formid::make(owner, static_cast<std::uint32_t>(k));
    return f ? CB::core::codec::formid::format(*f) : std::to_string(k);
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
    return q(key) + ":" + (v.empty() ? std::string(" {}\n") : block ? "\n" + indentLines(v, "  ", "  ") : " " + v + "\n");
}

/// @brief A `roster:` array's KEYED entries (`'key': body`, indent 0), in list order. Keys: the child node's id
///        (`ref`), an element string field (`field:`), the bone an element's index field names (`bone:`), or the minted
///        sub-record FormID (`id`/`slot`, from Identity::subKeys, else by position). The key field is not repeated in
///        the body; rosters nested inside an entry render inline in it.
/// @return nullopt when this list can't be keyed losslessly (a null / inline child in a `ref` roster, a duplicate
///         key, an unknown bone) — the caller keeps the plain list.
std::optional<std::string> rosterEntries(const io::SchemaObject& so, std::size_t i, const ReflectCtx& c) {
    const Field& f = *so.Fields()[i];
    const auto& objs = so.Values()[i].objs;
    const auto [kind, keyField] = rosterKind(f);
    const ReflectCtx ci = inlCtx(c);
    const std::vector<std::string>* keys = nullptr;
    if (auto it = c.id.subKeys.find({ &so, i }); it != c.id.subKeys.end() && it->second.size() == objs.size()) keys = &it->second;
    std::set<std::string> seen;
    std::string y;
    for (std::size_t k = 0; k < objs.size(); ++k) {
        const auto& p = objs[k];
        const auto* e = p ? dynamic_cast<const io::SchemaObject*>(p.get()) : nullptr;
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
                if (f.boneRef && n <= 1) {
                    const std::int64_t iv = decodeInt(v.raw, f.scalar);
                    if (iv != -1) y += f.name + ": " + boneText(iv, c) + "\n";
                } else if (n <= 1) {
                    y += f.name + ": " + scalarText(f, v.raw, c.reg) + "\n";
                    const long iv = static_cast<long>(decodeInt(v.raw, f.scalar));
                    if (!f.eventRef.empty()) { const std::string s = c.nr.safeEvent(iv);    if (!s.empty()) y += f.eventRef + ": " + q(s) + "\n"; }
                    if (!f.varRef.empty()) {
                        const std::string s = condTrue(so, f.charPropIf) ? c.nr.safe(c.nr.charPropsL, iv) : c.nr.safeVariable(iv);
                        if (!s.empty()) y += f.varRef + ": " + q(s) + "\n";
                    }
                } else {
                    y += f.name + ": " + flowList(n, [&](std::size_t k) { return scalarText(f, slice(v.raw, k, w), c.reg); }) + "\n";
                }
                break;
            }
            case FieldKind::String:
            case FieldKind::CString:    y += f.name + ": " + q(v.str) + "\n"; break;
            case FieldKind::Vector4:
            case FieldKind::Quaternion: y += f.name + ": " + rvec(v.raw) + "\n"; break;
            case FieldKind::QsTransform:
                y += f.name + ": " + flowList(3, [&](std::size_t k) { return rvec(slice(v.raw, k, 16)); }) + "\n";
                break;
            case FieldKind::Ptr:
            case FieldKind::Struct:      y += elemText(v.obj, c, f.name, f.ref); break;
            case FieldKind::PtrArray:
            case FieldKind::StructArray:
                if (!f.roster.empty() && !v.objs.empty())
                    if (auto r = rosterEntries(so, i, c)) {
                        if (c.sink) c.sink->push_back({ c.path + f.name, *r });
                        else y += f.name + ":\n" + indentLines(*r, "  ", "  ");
                        break;
                    }
                y += listText(v.objs, c, f.name, f.ref);
                break;
            case FieldKind::StringArray:
                if (v.strs.empty()) break;
                y += f.name + ": " + flowList(v.strs.size(), [&](std::size_t k) { return q(v.strs[k]); }) + "\n";
                break;
            case FieldKind::ScalarArray: {
                const std::size_t w = static_cast<std::size_t>(schema::ScalarWidth(f.scalar));
                const std::size_t n = w ? v.raw.size() / w : 0;
                if (n == 0) break;
                if (f.boneWeights && f.scalar == Scalar::Float) { y += boneWeightsText(f, v.raw, c); break; }
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

std::string reflectObject(const io::SchemaObject& so, const ReflectCtx& c, const std::string& declaredClass) {
    std::string y = (declaredClass == so.ClassName()) ? std::string() : "class: " + std::string(so.ClassName()) + "\n";
    if (!so.Schema()->vocab.empty() && vocabRenderable(so) && !isFileNode(ptrObj(so, "stringData"), c)) {
        if (so.Schema()->vocab == "graph") return y + renderVocab(so, c);
        const auto fields = so.Fields();
        for (std::size_t i = 0; i < fields.size(); ++i) {
            if (vocabField(so, fields[i]->name)) continue;
            if (fields[i]->name != "stringData") { y += renderField(so, i, c); continue; }
            const auto* sd = ptrObj(so, "stringData");
            std::string body;
            const auto sf = sd->Fields();
            for (std::size_t j = 0; j < sf.size(); ++j)
                if (sf[j]->name != kCharacterProperties.names) body += renderField(*sd, j, subCtx(c, "stringData"));
            y += body.empty() ? std::string("stringData: {}\n") : "stringData:\n" + indentLines(body, "  ", "  ");
        }
        return y + renderVocab(so, c);
    }
    const std::size_t n = so.Fields().size();
    for (std::size_t i = 0; i < n; ++i) y += renderField(so, i, c);
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
        for (const auto& [obj, cat] : identity.category)
            if (const auto* so = dynamic_cast<const io::SchemaObject*>(obj)) visit(*so);
        for (auto& [obj, cat] : effCat)
            if (cat.empty() && refs[obj] >= 2 && std::string(obj->ClassName()) != "hkRootLevelContainer") cat = "shared";
    }
    return effCat;
}

bool EmitHkyReflective(const Identity& identity, const schema::SchemaRegistry& reg,
                       const std::string& outDir, std::string& err, const std::vector<std::string>* bones) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const NameResolver nr = buildResolver(identity);
    const auto effCat = EffectiveCategories(identity);
    for (const auto& [obj, cat] : effCat) {
        const auto* so = dynamic_cast<const io::SchemaObject*>(obj);
        if (!so) { err = "EmitHkyReflective expects a SchemaObject graph"; return false; }
        const bool root = std::string(so->ClassName()) == "hkRootLevelContainer";
        if (cat.empty() && !root) continue;
        const std::string idStr = identity.ids.at(obj);
        std::vector<std::pair<std::string, std::string>> rosters;
        ReflectCtx ctx{ identity, nr, reg, bones, &effCat };
        if (!root) ctx.sink = &rosters;
        const std::string body = reflectObject(*so, ctx);
        const fs::path dir = root ? fs::path(outDir) : fs::path(outDir) / cat;
        fs::create_directories(dir, ec);
        std::ofstream(dir / (root ? std::string("graph.yaml") : fsId(idStr) + ".yaml"), std::ios::binary) << "id: " << idStr << "\n" << body;
        if (rosters.empty()) continue;
        const fs::path own = dir / fsId(idStr);
        fs::create_directories(own, ec);
        for (const auto& [path, entries] : rosters) std::ofstream(own / (path + ".yaml"), std::ios::binary) << entries;
    }
    return true;
}

bool DecompileBehaviorReflective(const std::vector<std::uint8_t>& bytes, const schema::SchemaRegistry& reg,
                                 const std::string& outDir, std::string& err, const std::vector<std::string>* bones) {
    try {
        PackFileDeserializer des;
        des.ObjectFactory = io::MakeSchemaFactory(reg);
        BinaryReaderEx br(false, true, bytes);
        des.Deserialize(br);
        return EmitHkyReflective(AssignIdentity(des, reg, ""), reg, outDir, err, bones);
    } catch (const std::exception& e) {
        err = std::string("DecompileBehaviorReflective: ") + e.what();
        return false;
    }
}

std::string EmitTagfile(const Identity& identity, const schema::SchemaRegistry& /*reg*/, std::string& /*err*/) {
    // objects in canonical id order (== tagfile #NNNN document order): numeric base ids ascending by
    // VALUE (byte-identical to the old int order), then any new-node ($) ids after, sorted lexically.
    std::vector<std::pair<std::string, const io::SchemaObject*>> ordered;
    for (const auto& [obj, idn] : identity.ids)
        if (const auto* so = dynamic_cast<const io::SchemaObject*>(obj)) ordered.push_back({ idn, so });
    std::sort(ordered.begin(), ordered.end(), [](const auto& a, const auto& b) {
        const bool an = allDigits(a.first), bn = allDigits(b.first);
        if (an && bn) return std::strtol(a.first.c_str(), nullptr, 10) < std::strtol(b.first.c_str(), nullptr, 10);
        if (an != bn) return an;                 // numeric (base) ids before new-node ids
        return a.first < b.first;
    });

    std::string x;
    for (const auto& [idn, so] : ordered) {
        char sig[16]; std::snprintf(sig, sizeof sig, "0x%x", so->Signature());
        x += "\t\t<hkobject name=\"" + tagRef(idn) + "\" class=\"" + std::string(so->ClassName()) +
             "\" signature=\"" + sig + "\">\n";
        tfBody(x, "\t\t\t", *so, identity);
        x += "\t\t</hkobject>\n";
    }
    return x;
}

// ── generic tagfile-XML PARSE (the inverse codec) ────────────────────────────────────────────────
namespace {
namespace en = CB::core::common::enums;
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

// A vec4/quaternion field's text -> 16 raw bytes (4 floats), via the shared membrane parser
// (CB::core::common::vec4::parseVec4) — the ONE vec4 text codec, also used by havok-core BehaviorBuilder::pv4.
// It accepts both the decompiler's "(x y z w)" and the bare/multi-line float form, so BR-28 (a bare
// axisOfRotation reading as 0 0 0 0) can't recur and the two paths can't diverge (B4 collapsed).
std::vector<std::uint8_t> parseVec4Raw(const std::string& t) {
    const auto q = CB::core::common::vec4::parseVec4(t);
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

// The enum table a scalar field renders through (schema `enum:` -> enumTable), else the tagfile-only
// overrides tfBody applies (variable-info type, role-attribute role). Mirrors tfBody's dispatch exactly.
const std::unordered_map<std::string, long>* scalarEnumTable(const std::string& cls, const Field& f, bool& isFlags) {
    isFlags = f.isFlags;
    if (!f.enumName.empty()) return enumTable(f.enumName);
    if (cls == "hkbVariableInfo"  && f.name == "type") return &en::VariableType();
    if (cls == "hkbRoleAttribute" && f.name == "role") return &en::Role();
    if (cls == "hkbRoleAttribute" && f.name == "flags") { isFlags = true; return &en::RoleFlags(); }
    // hkbExpressionData::eventMode is an ExpressionEventMode enum stored as a plain int8 in the schema
    // (kept out of the schema `enum:` to not perturb the class signature CRC). The tagfile authors it as
    // an enum NAME (e.g. EVENT_MODE_SEND_ON_TRUE); without this the name falls to parseL -> 0 (SEND_ONCE),
    // silently dropping BFCO's authored per-expression modes (AttackWinStart etc.).
    if (cls == "hkbExpressionData" && f.name == "eventMode") return &en::ExpressionEventMode();
    return nullptr;
}

// One scalar field's tagfile text -> raw bytes (inverse of renderScalar + tfBody's enum overrides).
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
const CB::core::common::xml::Node* paramByName(const CB::core::common::xml::Node& obj, const std::string& name) {
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

// Fill an already-created SchemaObject's fields from its tagfile <hkobject> node. `idMap` resolves refs
// by CANONICAL id (populated in pass 1, so every top-level id — base #NNNN or new-node #code$N — exists).
// Inline structs recurse.
void fillObject(io::SchemaObject& so, const CB::core::common::xml::Node& node,
                const schema::SchemaRegistry& reg,
                const std::unordered_map<std::string, std::shared_ptr<io::SchemaObject>>& idMap,
                const SymTab* syms, std::string& err) {
    const std::string cls = so.ClassName();
    const auto fields = so.Fields();
    for (std::size_t i = 0; i < fields.size(); ++i) {
        const Field& f = *fields[i];
        if (f.name.empty() || f.ignored) continue;         // vtable/skip/pad + SERIALIZE_IGNORED: no hkparam
        const CB::core::common::xml::Node* p = paramByName(node, f.name);
        if (!p) continue;                                  // field absent -> keep prior value (Init default,
                                                           //   or an earlier merge layer's value)
        io::FieldValue& val = so.FieldAt(i);
        val = io::FieldValue{};   // reset before (re)fill: a present field REPLACES — arrays must not
                                  // accumulate across merge layers, and a patch array is the whole array
        switch (f.kind) {
            case FieldKind::Scalar:  val.raw = parseScalarRaw(cls, f, p->text, syms, err); if (!err.empty()) return; break;
            case FieldKind::String:
            case FieldKind::CString: val.str = p->text; break;
            case FieldKind::Vector4:
            case FieldKind::Quaternion: val.raw = parseVec4Raw(p->text); break;
            case FieldKind::Ptr: {
                const std::string r = p->text;
                if (r.size() > 1 && r[0] == '#') {
                    auto it = idMap.find(canonId(r));
                    if (it != idMap.end()) val.obj = it->second;
                }
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
                for (const auto& c : p->children) if (c.tag == "hkcstring") val.strs.push_back(c.text);
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
                if (const CB::core::common::xml::Node* inner = p->child("hkobject")) {
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
void collectTopLevel(const CB::core::common::xml::Node& n, std::vector<const CB::core::common::xml::Node*>& out) {
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
bool anchorSnippets(const CB::core::common::xml::Node& base, const CB::core::common::xml::Node& layer,
                    const CB::core::common::xml::Node* orig, CB::core::common::xml::Node& out, std::string& err) {
    namespace xml = CB::core::common::xml;
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
            for (std::size_t k = 0; same && k < O.size(); ++k) same = CB::core::merge::deepEqual(O[k], B[k]);
            if (same) continue;
            const std::size_t n = O.size(), m = L.size();
            std::vector<std::uint32_t> dp((n + 1) * (m + 1), 0);
            for (std::size_t a = n; a-- > 0;)
                for (std::size_t b = m; b-- > 0;)
                    dp[a * (m + 1) + b] = CB::core::merge::deepEqual(O[a], L[b])
                        ? dp[(a + 1) * (m + 1) + b + 1] + 1
                        : std::max(dp[(a + 1) * (m + 1) + b], dp[a * (m + 1) + b + 1]);
            std::vector<xml::Node> fixed = L;
            bool changed = false;
            for (std::size_t a = 0, b = 0; a < n && b < m;) {
                if (CB::core::merge::deepEqual(O[a], L[b]) && dp[a * (m + 1) + b] == dp[(a + 1) * (m + 1) + b + 1] + 1) {
                    if (!CB::core::merge::deepEqual(O[a], B[a])) { fixed[b] = B[a]; changed = true; }
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
            for (std::size_t j = 0; eq && j < O.size(); ++j) eq = CB::core::merge::deepEqual(B[k + j], O[j]);
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
    std::vector<CB::core::common::xml::Node> roots; roots.reserve(sources.size());
    std::vector<CB::core::common::xml::Node> origRoots; origRoots.reserve(sources.size());   ///< deltas as their mod saw the base

    // Per CANONICAL id, the ORDERED layers that define it (base first, then each overriding patch). Each
    // layer records whether it is a delta and, for a delta, the fields it ACTUALLY changed (its MOD_CODE
    // delta, via the shared changedFields). The merge is NO LONGER a field-wise last-writer overlay —
    // every id's layers go through the SHARED CB::core::merge::bashMerge, so an array field edited by 2+
    // mods (variableNames/eventNames rosters, clip triggers, state/transition lists) is UNIONED exactly
    // as the runtime YAML loader does. Field-wise last-writer here silently dropped all-but-the-last
    // mod's array additions — the TDM_Pitch A-pose when one mod's several Nemesis codes are merged into
    // one bundle. New-node ids (#code$N) live in the same map as base #NNNN, so refs resolve either way.
    struct Layer { const CB::core::common::xml::Node* node; bool isDelta; const std::unordered_set<std::string>* changed;
                   const CB::core::common::xml::Node* orig = nullptr; };
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
            changedHold.push_back(std::make_unique<std::unordered_set<std::string>>(CB::core::merge::changedFields(*text)));
            changedPtr = changedHold.back().get();
            // Pandora-compat (CONVERTER-ONLY): reproduce Pandora/Nemesis's occurrence-counted
            // placement for numeric text-array params (boneWeights &c) BEFORE the positional
            // strip, so converted bundles match the ecosystem's output. Element params are left
            // for StripPatchOriginals below. See docs/bugs/CB-3.
            CB::core::compat::ApplyNemesisTextArrayEdits(parseText);
            CB::core::common::xml::StripPatchOriginals(parseText);
        }
        roots.push_back(CB::core::common::xml::Parse("<r>" + parseText + "</r>"));
        std::vector<const CB::core::common::xml::Node*> nodes;
        collectTopLevel(roots.back(), nodes);
        std::unordered_map<std::string, const CB::core::common::xml::Node*> origById;
        if (isDelta) {
            std::string origText = *text;
            CB::core::common::xml::StripToOriginal(origText);
            origRoots.push_back(CB::core::common::xml::Parse("<r>" + origText + "</r>"));
            std::vector<const CB::core::common::xml::Node*> on;
            collectTopLevel(origRoots.back(), on);
            for (const auto* o : on) origById[canonId(std::string(o->attr("name")))] = o;
        }
        for (const CB::core::common::xml::Node* n : nodes) {
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
    std::unordered_map<std::string, CB::core::common::xml::Node> mergedById;
    mergedById.reserve(order.size());
    for (const std::string& id : order) {
        const std::vector<Layer>& ls = layers[id];
        const std::string objCls(idMap[id]->ClassName());
        const CB::core::common::xml::Node* baseNode = nullptr;
        for (const Layer& L : ls) if (!L.isDelta) baseNode = L.node;   // the base node (normally exactly one)
        std::size_t seedSkip = 0;
        if (!baseNode) { baseNode = ls.front().node; seedSkip = 1; }   // new node: first layer seeds the merge

        std::vector<CB::core::merge::PatchLayer> pls; pls.reserve(ls.size());
        std::size_t seen = 0;
        for (const Layer& L : ls) {
            const bool isSeed = (seedSkip == 1 && seen++ == 0);
            if (!L.isDelta || isSeed) continue;
            if (std::string(L.node->attr("class")) != objCls) continue;   // source drift — skip this layer
            CB::core::common::xml::Node anchored;
            std::string aerr;
            const bool expanded = seedSkip == 0 && anchorSnippets(*baseNode, *L.node, L.orig, anchored, aerr);
            if (!aerr.empty()) { err = "parseSourcesMerged: " + aerr; return false; }
            pls.push_back({ expanded ? std::move(anchored) : *L.node, L.changed ? *L.changed : std::unordered_set<std::string>{} });
        }
        std::vector<const CB::core::merge::PatchLayer*> ptrs; ptrs.reserve(pls.size());
        for (const auto& p : pls) ptrs.push_back(&p);
        // compose predicate: read the field's `merge:` tag from the SAME SchemaRegistry the runtime
        // merge reads (SchemaRegistry::MergeTag) — so converter and runtime compose identically.
        auto composePred = [&reg](const std::string& c, const std::string& f) {
            return reg.MergeTag(c, f) == "compose";
        };
        mergedById.emplace(id, ptrs.empty() ? *baseNode
                               : CB::core::merge::bashMerge(*baseNode, ptrs, /*guarded*/ {}, composePred));
    }

    // Nemesis symbol roster: variable/event NAME -> merged index, from the MERGED hkbBehaviorGraphStringData
    // (now the UNIONED roster across all mods, so a name only one mod added still resolves). Bindings /
    // transitions written as $variableID[Name]$ / $eventID[Name]$ resolve against it in pass 2. First
    // string-data wins (the graph's own). Built BEFORE pass 2 so it's ready when a binding field is parsed.
    SymTab syms;
    for (const std::string& id : order) {
        const CB::core::common::xml::Node& n = mergedById[id];
        if (std::string(n.attr("class")) != "hkbBehaviorGraphStringData") continue;
        auto fill = [&](const char* param, std::unordered_map<std::string, int>& m) {
            if (const CB::core::common::xml::Node* p = paramByName(n, param)) {
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
float parseFloatStrict(const std::string& t) { float f; const auto b = floatBytes(t); std::memcpy(&f, b.data(), 4); return f; }
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
    switch (f.kind) {
        case FieldKind::Scalar: {
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
        case FieldKind::Quaternion: v.raw = vec4Strict(ystr(n)); return true;
        case FieldKind::QsTransform:
            for (auto e : n) { const auto b = vec4Strict(ystr(e)); v.raw.insert(v.raw.end(), b.begin(), b.end()); }
            return true;
        case FieldKind::Ptr:
        case FieldKind::Struct:     return objectValue(n, f.ref, v.obj, ps, f.name);
        case FieldKind::PtrArray:
        case FieldKind::StructArray:
            for (auto e : n) {
                std::shared_ptr<IHavokObject> o;
                if (!objectValue(e, f.ref, o, ps, f.name)) return false;
                v.objs.push_back(o);
            }
            return true;
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
                if (f.boneList) { std::int64_t idx = -1; if (!boneIndex(ystr(e), idx, ps, f.name)) return false; b = encInt(idx, f.scalar); }
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
    const auto fs = so.Fields();
    for (std::size_t j = 0; j < fs.size(); ++j) {
        if (fs[j]->name != field) continue;
        io::FieldValue& fv = so.FieldAt(j);
        if (!fv.obj) {
            auto o = makeObj(ps.reg, fs[j]->ref);
            if (!o) return nullptr;
            ps.all.push_back(o);
            fv.obj = o;
        }
        return dynamic_cast<io::SchemaObject*>(fv.obj.get());
    }
    return nullptr;
}

/// @brief One variable / character-property record onto `info` (+ its word, and its quad for vector types). `fresh`
///        = a new entry (needs `type:`); an existing one keeps what its record leaves out.
bool applyInfoRecord(io::SchemaObject& info, const yml::ConstNodeRef& rec, bool fresh, io::SchemaObject* word,
                     std::vector<std::uint8_t>& quads, std::vector<std::shared_ptr<IHavokObject>>* variants,
                     const std::string& what, HkyParse& ps) {
    auto make = [&](const char* cls) { auto o = makeObj(ps.reg, cls); ps.all.push_back(o); return o; };
    auto storage = [](long t) { return isPointerType(t) ? 2 : isQuadType(t) ? 1 : 0; };
    {
        long type = fInt(info, "type");
        if (yhas(rec, "type")) {
            long t = 0;
            if (!enumParse("VariableType", ystr(yget(rec, "type")), t, ps, what)) return false;
            if (!fresh && storage(t) != storage(type)) { ps.err = ps.where + ": " + what + ": changing a variable's storage (word / quad / pointer) is not supported"; return false; }
            type = t;
            info.FieldRef("type").raw = encInt(type, Scalar::Int8);
        } else if (fresh) { ps.err = ps.where + ": " + what + ": a new entry needs `type:`"; return false; }
        io::FieldValue& rf = info.FieldRef("role");
        if (!rf.obj) rf.obj = make("hkbRoleAttribute");
        auto* role = dynamic_cast<io::SchemaObject*>(rf.obj.get());
        if (yhas(rec, "role"))  { long r = 0;  if (!enumParse("Role", ystr(yget(rec, "role")), r, ps, what)) return false; role->FieldRef("role").raw = encInt(r, Scalar::Int16); }
        if (yhas(rec, "flags")) { long fl = 0; if (!enumParse("RoleFlags", ystr(yget(rec, "flags")), fl, ps, what)) return false; role->FieldRef("flags").raw = encInt(fl, Scalar::Int16); }
        if (!word) return true;
        if (isPointerType(type)) {
            if (!variants) { ps.err = ps.where + ": " + what + ": a pointer value needs a value set"; return false; }
            std::size_t vi = variants->size();
            if (fresh) { word->FieldRef("value").raw = encInt(static_cast<std::int64_t>(vi), Scalar::Int32); variants->push_back(nullptr); }
            else vi = static_cast<std::size_t>(fInt(*word, "value"));
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
            if (fresh) { word->FieldRef("value").raw = encInt(static_cast<std::int64_t>(qi), Scalar::Int32); quads.resize(quads.size() + 16, 0); }
            else qi = static_cast<std::size_t>(fInt(*word, "value"));
            if (yhas(rec, "value") && qi * 16 + 16 <= quads.size()) {
                const auto b = vec4Strict(ystr(yget(rec, "value")));
                std::memcpy(quads.data() + qi * 16, b.data(), 16);
            }
            return true;
        }
        if (yhas(rec, "value")) {
            const std::string t = ystr(yget(rec, "value"));
            if (type == 4) word->FieldRef("value").raw = floatBytes(t);
            else if (t == "true" || t == "false") word->FieldRef("value").raw = encInt(t == "true" ? 1 : 0, Scalar::Int32);
            else {
                char* end = nullptr;
                const long long n = std::strtoll(t.c_str(), &end, 0);
                if (!end || *end) { ps.err = ps.where + ": " + what + ": value '" + t + "' is not a number"; return false; }
                word->FieldRef("value").raw = encInt(n, Scalar::Int32);
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
                info->FieldRef("flags").raw = encInt(fl, Scalar::UInt32);
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
    for (std::size_t i = 0; i < fields.size(); ++i) {
        const Field& f = *fields[i];
        if (f.name.empty() || (f.ignored && !f.hkyEmit)) continue;
        if (!yhas(map, f.name) && ps.patch) continue;   // a patch touches only what it names
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

bool ParseHkyReflective(const std::string& dir, const schema::SchemaRegistry& reg, ParsedHky& out, std::string& err,
                        const std::vector<std::string>* bones) {
    return ParseHkyReflective(CB::core::common::DiskUnitSource(dir), reg, out, err, bones);
}

namespace {
/// @brief One YAML file of a unit. A ROSTER file (`<cat>/<id>/<field.path>.yaml`) carries one roster of node `owner`
///        at `field` (the root's rosters stay inline in graph.yaml); anything else is a node file.
struct HkyDoc { std::string text; yml::Tree tree; std::string cat; std::string file; bool roster = false; std::string owner, field; };
/// @brief Every YAML file of a unit, parsed in place (the text buffer lives beside its tree).
std::vector<std::unique_ptr<HkyDoc>> loadHkyDocs(const CB::core::common::IUnitSource& src) {
    std::vector<std::unique_ptr<HkyDoc>> docs;
    for (const std::string& rel : src.listYaml("", true)) {
        auto text = src.read(rel);
        if (!text) continue;
        auto d = std::make_unique<HkyDoc>();
        d->text = std::move(*text);
        d->file = rel;
        const auto slash = rel.rfind('/'), first = rel.find('/');
        d->cat = slash == std::string::npos ? std::string() : rel.substr(0, slash);
        if (slash != std::string::npos) {
            std::string stem = rel.substr(slash + 1);
            if (stem.size() > 5 && stem.compare(stem.size() - 5, 5, ".yaml") == 0) stem.resize(stem.size() - 5);
            if (first != slash) {
                d->roster = true;
                d->owner = d->cat.substr(d->cat.rfind('/') + 1);
                for (char& ch : d->owner) if (ch == '-') ch = ':';
                d->field = stem;
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
        obj = next;
        s = dot + 1;
    }
}
} // namespace

bool ParseHkyReflective(const CB::core::common::IUnitSource& src, const schema::SchemaRegistry& reg, ParsedHky& out,
                        std::string& err, const std::vector<std::string>* bones) {
    HkyParse ps{ reg, bones, {}, {}, {}, {}, {} };
    auto docs = loadHkyDocs(src);
    // Pass 1: create every file node (and the root) so ids resolve regardless of file order.
    std::shared_ptr<io::SchemaObject> root;
    for (auto& d : docs) {
        if (d->roster) continue;
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
    for (auto& d : docs) {
        if (d->roster) continue;
        ps.where = d->file;
        const auto r = d->tree.crootref();
        if (!fillHky(*ps.byId.at(ystr(yget(r, "id"))), r, ps)) { err = ps.err; return false; }
    }
    for (auto& d : docs) {
        if (!d->roster) continue;
        const auto it = ps.byId.find(d->owner);
        io::SchemaObject* owner = it == ps.byId.end() ? nullptr : it->second.get();
        if (!owner) { err = d->file + ": no node '" + d->owner + "' for this roster"; return false; }
        if (!applyRosterFile(*owner, *d, false, ps)) { err = ps.err; return false; }
    }
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
        if (const auto f = CB::core::codec::formid::make(idx, static_cast<std::uint32_t>(v)))
            return CB::core::codec::formid::format(*f);
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
        if (wb != wm) y += "  " + q((*c.bones)[k]) + ": " + wm + "\n";
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
        if (std::find(b->begin(), b->end(), bi) == b->end()) y += "  " + q((*c.bones)[static_cast<std::size_t>(bi)]) + ": {}\n";
    for (const std::int64_t bi : *b)
        if (std::find(m->begin(), m->end(), bi) == m->end()) y += "  " + q((*c.bones)[static_cast<std::size_t>(bi)]) + ": {deleted: true}\n";
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
    std::string y = "type: " + enumText("VariableType", fInt(*info, "type"), false) + "\n";
    if (const auto* role = ptrObj(*info, "role")) {
        y += "role: " + enumText("Role", fInt(*role, "role"), false) + "\n";
        y += "flags: " + enumText("RoleFlags", static_cast<long>(static_cast<std::uint16_t>(fInt(*role, "flags"))), true) + "\n";
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
        if (!claimed[bk]) y += q(baseKey(bk)) + ": {deleted: true}\n";
    return y;
}

/// @brief The field-level patch turning `b` into `m` (empty = identical). Nested inline objects of the same class
///        deep-diff; compose/union arrays whose base is a prefix emit only the appended tail (else `{replace: [...]}`);
///        a value that became absent is written explicitly (`null` / `[]` / -1) — a patch never relies on omission.
std::string diffObject(const io::SchemaObject& b, const io::SchemaObject& m, const ReflectCtx& cb, const ReflectCtx& cm,
                       const FieldSkip& skip) {
    std::string y;
    if (!skip && !m.Schema()->vocab.empty() && vocabRenderable(b) && vocabRenderable(m, true) &&
        !isFileNode(ptrObj(b, "stringData"), cb) && !isFileNode(ptrObj(m, "stringData"), cm)) {
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
        const std::string sd = diffObject(*ptrObj(b, "stringData"), *ptrObj(m, "stringData"), subCtx(cb, "stringData"),
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
            for (const auto& s2 : vm.strs) if (!have.count(s2)) list += (list.empty() ? "" : ", ") + q(s2);
            if (!list.empty()) y += f.name + ": [" + list + "]\n";
            continue;
        }
        y += tm;
    }
    return y;
}
} // namespace

bool DeriveHkyPatch(const std::vector<std::uint8_t>& baseBytes, const std::vector<std::uint8_t>& modBytes,
                    const schema::SchemaRegistry& reg, const std::string& patchDir, std::string& err,
                    const std::vector<std::string>* bones, std::uint16_t baseIndex, HkyPatchStats* stats) {
    namespace fs = std::filesystem;
    try {
        PackFileDeserializer desB, desM;
        desB.ObjectFactory = io::MakeSchemaFactory(reg);
        desM.ObjectFactory = io::MakeSchemaFactory(reg);
        { BinaryReaderEx br(false, true, baseBytes); desB.Deserialize(br); }
        { BinaryReaderEx br(false, true, modBytes);  desM.Deserialize(br); }
        Identity idB = AssignIdentity(desB, reg, ""), idM = AssignIdentity(desM, reg, "");
        for (auto& [o, id] : idB.ids) id = formIdOf(baseIndex, id);
        const auto effB = EffectiveCategories(idB), effM = EffectiveCategories(idM);
        const NameResolver nrB = buildResolver(idB), nrM = buildResolver(idM);
        auto isRoot = [](const IHavokObject* o) { return std::string(o->ClassName()) == "hkRootLevelContainer"; };
        auto nameOf = [](const IHavokObject* o) {
            const auto* so = dynamic_cast<const io::SchemaObject*>(o);
            return so ? fStr(*so, "name") : std::string();
        };
        const ReflectCtx cB{ idB, nrB, reg, bones, &effB, baseIndex };

        // Base lookups: named nodes by (class, name); unnamed shared leaves by rendered content; the root.
        std::map<std::pair<std::string, std::string>, const IHavokObject*> baseNamed;
        std::map<std::string, const IHavokObject*> baseContent;
        std::unordered_map<std::string, const IHavokObject*> baseById;
        const IHavokObject* baseRoot = nullptr;
        for (const auto& [o, cat] : effB) {
            if (cat.empty() && !isRoot(o)) continue;
            baseById[idB.ids.at(o)] = o;
            if (isRoot(o)) { baseRoot = o; continue; }
            if (const std::string n = nameOf(o); !n.empty()) baseNamed[{ o->ClassName(), n }] = o;
            else if (const auto* so = dynamic_cast<const io::SchemaObject*>(o))
                baseContent[std::string(o->ClassName()) + "\n" + reflectObject(*so, cB)] = o;
        }

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
        std::unordered_map<const IHavokObject*, std::string> modId;
        int seq = 0;
        for (const IHavokObject* o : modNodes) {
            const IHavokObject* match = nullptr;
            if (isRoot(o)) match = baseRoot;
            else if (const std::string n = nameOf(o); !n.empty()) {
                if (auto it = baseNamed.find({ o->ClassName(), n }); it != baseNamed.end()) match = it->second;
            } else if (const auto* so = dynamic_cast<const io::SchemaObject*>(o)) {
                if (auto it = baseContent.find(std::string(o->ClassName()) + "\n" + reflectObject(*so, cM0)); it != baseContent.end())
                    match = it->second;
            }
            if (match && !taken.count(match)) { taken.insert(match); modId[o] = idB.ids.at(match); }
            else modId[o] = formIdOf(CB::core::codec::formid::SELF_INDEX, std::to_string(++seq));
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
            const fs::path dir = isRoot(o) ? fs::path(patchDir) : fs::path(patchDir) / cat;
            fs::create_directories(dir, ec);
            /// @note A patch states its class for the reader (a new node's body already carries it). A node whose only
            ///       change is a roster ships just that roster file.
            if (!body.empty() || !baseById.count(id))
                std::ofstream(dir / (isRoot(o) ? std::string("graph.yaml") : fsId(id) + ".yaml"), std::ios::binary)
                    << "id: " << id << "\n" << (baseById.count(id) ? "class: " + std::string(mo->ClassName()) + "\n" : std::string())
                    << body;
            if (rosters.empty()) continue;
            const fs::path own = dir / fsId(id);
            fs::create_directories(own, ec);
            for (const auto& [path, entries] : rosters) std::ofstream(own / (path + ".yaml"), std::ios::binary) << entries;
        }
        return true;
    } catch (const std::exception& e) {
        err = std::string("DeriveHkyPatch: ") + e.what();
        return false;
    }
}

namespace {
/// @brief A unit's id -> canonical owner key: FormId "idx:local" resolves through the unit's master table
///        (`masters[idx]:local`); anything else stays verbatim.
std::function<std::string(const std::string&)> canonFor(const std::vector<std::string>& masters) {
    return [masters](const std::string& id) {
        if (const auto f = CB::core::codec::formid::parse(id); f && f->masterIndex < masters.size())
            return masters[f->masterIndex] + ":" + std::to_string(f->local);
        return id;
    };
}
} // namespace

bool MergeHkyPatch(ParsedHky& base, const CB::core::common::IUnitSource& patch, const schema::SchemaRegistry& reg,
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
                    const io::FieldValue* fv = fieldByName(*sd, s.isEvent ? "eventNames" : "variableNames");
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
        Identity idB = AssignIdentity(desB, reg, "");
        for (auto& [o, id] : idB.ids) id = formIdOf(CB::core::codec::formid::SELF_INDEX, id);
        if (!EmitHkyReflective(idB, reg, (fs::path(workDir) / "base").string(), err, bones)) return false;
        ParsedHky merged;
        if (!ParseHkyReflective((fs::path(workDir) / "base").string(), reg, merged, err, bones)) return false;
        for (std::size_t k = 0; k < patchDirs.size(); ++k) {
            const std::string self = "mod" + std::to_string(k + 1);
            if (!MergeHkyPatch(merged, CB::core::common::DiskUnitSource(patchDirs[k]), reg, { "base" }, { self, "base" }, err, bones)) {
                err = patchDirs[k] + ": " + err;
                return false;
            }
        }
        PackFileSerializer ser;
        BinaryWriterEx bw(header.Endian == 0, header.PointerSize == 8);
        ser.Serialize(merged.root, bw, header);
        const auto out = bw.Data();
        std::ofstream(fs::path(workDir) / "merged.hkx", std::ios::binary)
            .write(reinterpret_cast<const char*>(out.data()), static_cast<std::streamsize>(out.size()));
        PackFileDeserializer desM;
        desM.ObjectFactory = io::MakeSchemaFactory(reg);
        BinaryReaderEx brM(false, true, out);
        desM.Deserialize(brM);
        return EmitHkyReflective(AssignIdentity(desM, reg, ""), reg, (fs::path(workDir) / "merged").string(), err, bones);
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
        Identity idB = AssignIdentity(desB, reg, "");
        for (auto& [o, id] : idB.ids) id = formIdOf(CB::core::codec::formid::SELF_INDEX, id);
        if (!EmitHkyReflective(idB, reg, (fs::path(workDir) / "base").string(), r.error, bones)) return r;
        // The mod's PATCH, seen from the mod bundle (self = 0, the base = master 1).
        if (!DeriveHkyPatch(baseBytes, modBytes, reg, (fs::path(workDir) / "patch").string(), r.error, bones, 1, &r.derived)) return r;
        ParsedHky merged;
        if (!ParseHkyReflective((fs::path(workDir) / "base").string(), reg, merged, r.error, bones)) return r;
        if (!MergeHkyPatch(merged, CB::core::common::DiskUnitSource((fs::path(workDir) / "patch").string()), reg,
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
            std::ofstream(fs::path(workDir) / "want.bin", std::ios::binary).write(reinterpret_cast<const char*>(want.data()), static_cast<std::streamsize>(want.size()));
            std::ofstream(fs::path(workDir) / "got.bin",  std::ios::binary).write(reinterpret_cast<const char*>(got.data()),  static_cast<std::streamsize>(got.size()));
            std::string e2;
            EmitHkyReflective(AssignIdentity(desM, reg, ""), reg, (fs::path(workDir) / "want").string(), e2, bones);
            PackFileDeserializer desG;
            desG.ObjectFactory = io::MakeSchemaFactory(reg);
            BinaryReaderEx brG(false, true, got);
            desG.Deserialize(brG);
            EmitHkyReflective(AssignIdentity(desG, reg, ""), reg, (fs::path(workDir) / "got").string(), e2, bones);
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
        Identity idB = AssignIdentity(desB, reg, "");
        for (auto& [o, id] : idB.ids) id = formIdOf(CB::core::codec::formid::SELF_INDEX, id);
        if (!EmitHkyReflective(idB, reg, (fs::path(workDir) / "base").string(), r.error, bones)) return r;
        ParsedHky merged;
        if (!ParseHkyReflective((fs::path(workDir) / "base").string(), reg, merged, r.error, bones)) return r;
        for (const auto& m : mods) {
            ParsedTagfile one; std::vector<std::string> oneIds;
            if (!MergeTagfiles(baseXml, m.patches, reg, one, oneIds, r.error)) { r.error = m.name + ": " + r.error; return r; }
            const fs::path pd = fs::path(workDir) / ("patch_" + m.name);
            HkyPatchStats st;
            if (!DeriveHkyPatch(baseBytes, serialize(rootOf(one)), reg, pd.string(), r.error, bones, 1, &st)) { r.error = m.name + ": " + r.error; return r; }
            if (!MergeHkyPatch(merged, CB::core::common::DiskUnitSource(pd.string()), reg, { "base" }, { m.name, "base" }, r.error, bones)) {
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
                EmitHkyReflective(AssignIdentity(d1, reg, ""), reg, (fs::path(workDir) / ("one_" + m.name)).string(), e2, bones);
            }
        }
        const auto got = serialize(merged.root);
        r.identical = (got == want);
        if (!r.identical) {
            std::ofstream(fs::path(workDir) / "want.bin", std::ios::binary).write(reinterpret_cast<const char*>(want.data()), static_cast<std::streamsize>(want.size()));
            std::ofstream(fs::path(workDir) / "got.bin",  std::ios::binary).write(reinterpret_cast<const char*>(got.data()),  static_cast<std::streamsize>(got.size()));
            std::string e2;
            for (const auto& [name, bytes] : { std::pair<const char*, const std::vector<std::uint8_t>*>{ "want", &want }, { "got", &got } }) {
                PackFileDeserializer d;
                d.ObjectFactory = io::MakeSchemaFactory(reg);
                BinaryReaderEx br(false, true, *bytes);
                d.Deserialize(br);
                EmitHkyReflective(AssignIdentity(d, reg, ""), reg, (fs::path(workDir) / name).string(), e2, bones);
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
                                       const std::string& workDir, const std::vector<std::string>* bones) {
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
        if (!EmitHkyReflective(AssignIdentity(des, reg, ""), reg, workDir, r.error, bones)) return r;
        ParsedHky parsed;
        if (!ParseHkyReflective(workDir, reg, parsed, r.error, bones)) return r;
        const std::vector<std::uint8_t> back = serialize(parsed.root);
        r.identical = (back == canon);
        if (!r.identical) {
            const std::size_t n = std::min(back.size(), canon.size());
            std::size_t i = 0;
            while (i < n && back[i] == canon[i]) ++i;
            r.firstDiff = i;
            r.sizeDelta = static_cast<long long>(back.size()) - static_cast<long long>(canon.size());
            std::ofstream(std::filesystem::path(workDir) / "canon.bin", std::ios::binary)
                .write(reinterpret_cast<const char*>(canon.data()), static_cast<std::streamsize>(canon.size()));
            std::ofstream(std::filesystem::path(workDir) / "back.bin", std::ios::binary)
                .write(reinterpret_cast<const char*>(back.data()), static_cast<std::streamsize>(back.size()));
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

ModDeltaResult ConvertModDelta(const std::string& baseTagfileXml, const std::vector<std::string>& patchDirs,
                               const schema::SchemaRegistry& reg, const std::string& outDeltaDir,
                               const NemesisFormIdCtx* fid) {
    namespace fs = std::filesystem;
    ModDeltaResult r;

    // Gather every Nemesis patch node (#*.txt) across the mod's dirs, IN LOAD ORDER. Pass the RAW text
    // (MOD_CODE markers intact): MergeTagfiles -> parseSourcesMerged strips for the node parse AND reads
    // the markers via changedFields, so the bashMerge unions only each mod's ACTUAL edits (matching the
    // typed PatchConverter). Stripping here would erase the markers and force the all-fields fallback.
    std::vector<std::string> patches;
    std::error_code ec;
    for (const std::string& dir : patchDirs) {
        if (!fs::is_directory(dir, ec)) continue;
        std::vector<fs::path> files;
        for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
            if (!it->is_regular_file()) continue;
            const std::string fn = it->path().filename().string();
            if (!fn.empty() && fn[0] == '#' && it->path().extension() == ".txt") files.push_back(it->path());
        }
        std::sort(files.begin(), files.end());   // stable, deterministic order within a dir
        for (const fs::path& f : files) {
            std::ifstream in(f, std::ios::binary);
            std::string s((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            if (s.empty()) continue;
            patches.push_back(std::move(s));   // RAW — parseSourcesMerged strips + reads MOD_CODE
        }
    }
    r.patchNodes = static_cast<int>(patches.size());
    if (patches.empty()) { r.ok = true; return r; }   // this mod doesn't touch this graph

    ParsedTagfile merged; std::vector<std::string> deltaIds; std::string err;
    if (!MergeTagfiles(baseTagfileXml, patches, reg, merged, deltaIds, err)) { r.error = "merge: " + err; return r; }
    r.deltaIds = static_cast<int>(deltaIds.size());

    std::set<std::string> ds(deltaIds.begin(), deltaIds.end());

    // FormId minting (fid != nullptr). APPLY the converter's pre-built code->index map (built once for the
    // whole bundle, so a master's index is identical in every graph). Each id -> a FormId "IIIIxLLLL":
    // bare "184" -> "0000x00b8" (vanilla edit, index 0); "<code>$N" -> "<codeIndex[code]>x<N>". Both
    // identity.ids AND ds (EmitHky's writeIds gate) are rewritten in lockstep — EmitHky keys byId off the
    // transformed identity.
    if (fid) {
        auto low = [](std::string s) { for (char& c : s) c = static_cast<char>(std::tolower((unsigned char)c)); return s; };
        std::set<std::string> unknownCodes;
        auto toFormId = [&](const std::string& raw) -> std::string {
            const auto d = raw.rfind('$');
            char* end = nullptr;
            if (d == std::string::npos) {                                 // bare -> vanilla node (origin = Skyrim, index 1)
                const unsigned long v = std::strtoul(raw.c_str(), &end, 10);
                if (end && *end == '\0')
                    if (const auto f = CB::core::codec::formid::make(CB::core::codec::formid::BASE_GAME_INDEX, static_cast<std::uint32_t>(v))) return CB::core::codec::formid::format(*f);
                return raw;                                               // non-numeric bare (name key) -> as-is
            }
            const std::string code = low(raw.substr(0, d));
            const unsigned long n = std::strtoul(raw.c_str() + d + 1, &end, 10);
            if (!end || *end != '\0') return raw;                        // malformed $N
            std::uint16_t idx = fid->selfIndex;
            if (const auto it = fid->codeIndex.find(code); it != fid->codeIndex.end()) idx = it->second;
            else unknownCodes.insert(code);                              // unknown foreign -> self + warn
            if (const auto f = CB::core::codec::formid::make(idx, static_cast<std::uint32_t>(n))) return CB::core::codec::formid::format(*f);
            return raw;
        };
        std::unordered_map<std::string, std::string> remap;
        for (auto& [obj, raw] : merged.identity.ids) { (void)obj; const std::string v = toFormId(raw); remap[raw] = v; raw = v; }
        std::set<std::string> newDs;
        for (const std::string& d : ds) { const auto it = remap.find(d); newDs.insert(it != remap.end() ? it->second : d); }
        ds = std::move(newDs);
        for (const std::string& c : unknownCodes)
            r.warnings.push_back("FormId: unknown foreign mod code '" + c + "$..' not in this bundle's master table; "
                                 "bound to self (a cross-bundle reference to it will not reconcile — is that mod converted?)");
    }

    if (!EmitHky(merged.identity, reg, outDeltaDir, err, &ds, &r.warnings)) { r.error = "emit: " + err; return r; }

    // Added-vocabulary sidecar — diff the base graph vocab against the merged graph's.
    ParsedTagfile baseParsed;
    if (ParseTagfile(baseTagfileXml, reg, baseParsed, err))
        if (!EmitAdditiveVocab(baseParsed.identity, merged.identity, outDeltaDir, err)) { r.error = "additive: " + err; return r; }

    r.ok = true;
    return r;
}

} // namespace CB::core::decompile
