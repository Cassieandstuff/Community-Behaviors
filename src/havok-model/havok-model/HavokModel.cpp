#include <havok-model/HavokModel.h>

#include "havok/sct/TagfileOracle.h"      // AlignTagfile — the structural #NNNN oracle (reused verbatim)
#include "havok/model/HavokEnums.h"       // enum tables (value<->name), revNum, FormatFlags, ResolveEnum — reused
#include "havok/xml/Xml.h"                // first-party tagfile XML parser (xml::Node / xml::Parse)
#include "havok/model/BashMerge.h"        // shared merge core (bashMerge / decideParam / changedFields) — lockstep w/ runtime
#include "havok/cross/Cross.h"            // cross-kind membrane (vec4 codec, enum reverse, bone name<->index)

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <set>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// Stage 3. Increment 1 scaffolded the lib; increment 2 (here) is the identity/index pass +
// the class→category map. Increment 3 fills in EmitHky. See HavokModel.h for the architecture.

namespace havok::model {

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
        const havok::sct::OracleResult res = havok::sct::AlignTagfile(des, xmlText);
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
namespace en = havok::model::enums;
using schema::Field;
using schema::FieldKind;
using schema::Scalar;

std::string fstr(float v) { char b[32]; std::snprintf(b, sizeof b, "%.9g", v); return b; }
std::string boolstr(bool v) { return v ? "true" : "false"; }
// enum value -> name, else the raw number (round-trips: numbers parse back). Via the shared membrane
// codec (havok::cross::enumName), so this and the decompiler's revNum are one rule — and deterministic
// on aliases (B5), unlike the old arbitrary unordered_map scan.
std::string revNum(const std::unordered_map<std::string, long>& t, long v) {
    std::string nm = havok::cross::enumName(v, t);
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
const std::unordered_map<std::string, long>* enumTable(const std::string& n) {
    static const std::unordered_map<std::string, const std::unordered_map<std::string, long>*> reg = {
        {"PlaybackMode",                  &en::PlaybackMode()},
        {"VariableMode",                  &en::VariableMode()},
        {"StartStateMode",                &en::StartStateMode()},
        {"StateMachineSelfTransitionMode", &en::SmSelfTransitionMode()},  // annotation name != en:: name
        {"SelfTransitionMode",            &en::SelfTransitionMode()},
        {"EventMode",                     &en::EventMode()},
        {"EndMode",                       &en::EndMode()},
        {"BlendCurve",                    &en::BlendCurve()},
        {"BlendModeFunction",             &en::BlendModeFunction()},
        {"SetAngleMethod",                &en::SetAngleMethod()},
        {"RotationAxisCoordinates",       &en::RotationAxisCoordinates()},
        {"BindingType",                   &en::BindingType()},
        {"TransitionFlags",               &en::TransitionFlags()},
        {"FlagBits",                      &en::FlagBits()},
        {"ClipGeneratorFlags",            &en::ClipGeneratorFlags()},
        {"BlenderFlags",                  &en::BlenderFlags()},
    };
    auto it = reg.find(n);
    return it == reg.end() ? nullptr : it->second;
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
    // index -> name via the shared membrane primitive (havok::cross::rosterName) — one bounds-checked
    // lookup rule with the emit side's eventName/variableName/charPropName.
    std::string event(long i) const    { return havok::cross::rosterName(static_cast<int>(i), events); }
    std::string variable(long i) const { return havok::cross::rosterName(static_cast<int>(i), variables); }
    std::string charProp(long i) const { return havok::cross::rosterName(static_cast<int>(i), charProps); }
    // Merge-safe name: only if `i` is the FIRST index carrying that name (a duplicate name would
    // remap on round-trip, so the decompiler falls back to the raw id). Matches safeEventName.
    std::string safe(const std::vector<std::string>& tbl, long i) const {
        if (i < 0 || i >= static_cast<long>(tbl.size())) return {};
        const std::string& n = tbl[i];
        if (n.empty()) return {};
        for (long j = 0; j < i; ++j) if (tbl[j] == n) return {};   // earlier duplicate -> not safe
        return n;
    }
    std::string safeEvent(long i) const    { return safe(events, i); }
    std::string safeVariable(long i) const { return safe(variables, i); }
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
// id of a referenced node (or "" if null / not identified).
std::string refIdOf(const std::shared_ptr<IHavokObject>& p, const Identity& id) {
    if (!p) return {};
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

// Write every data/ sidecar a file-node modifier owns (id keys the file: data/<id>_<suffix>.yaml). Called
// right after the modifier's own YAML so the sidecar always ships with it (full emit AND per-mod delta).
void emitModifierSidecars(const io::SchemaObject& so, const std::string& id, const std::string& fnameBase,
                          const std::filesystem::path& outDir, const NameResolver& nr) {
    namespace fs = std::filesystem;
    std::error_code ec;
    // Each sidecar is a .hky node: `id: <id>_<suffix>` then the body. (Matches DecompileNativeDelta.)
    // The id: field carries the (class,name) editorId + suffix (the loader's `<ownerKey>_<suffix>` lookup);
    // the FILENAME is a numeric base (an editorId contains ':' — illegal in a filename).
    auto put = [&](const std::string& suffix, const std::string& body) {
        if (body.empty()) return;
        const fs::path dir = outDir / "data";
        fs::create_directories(dir, ec);
        std::ofstream(dir / (fnameBase + suffix + ".yaml"), std::ios::binary) << "id: " << id << suffix << "\n" << body;
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
                if (sval.empty()) sval = renderScalar(f, v);
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

// Remap a merged graph's tagfile-#NNNN identity to CLASS-QUALIFIED (class,name) editorIds — the same
// scheme the base master emits (breezy-gliding-nebula) — so per-mod deltas merge onto the base by stable
// name identity instead of colliding numbers. A named node -> "<class>:<name>"; a state ->
// "hkbStateMachineStateInfo:<owningSM>_<name>" (state names are unique only within their SM). Nameless /
// inline objects (arrays, binding sets, conditions) keep their #NNNN — they are folded into an owner and
// are never a ref target, so they never appear as an id in the emitted refs. deltaIds remap in lockstep.
void RemapToEditorIds(Identity& identity, std::set<std::string>& deltaIds) {
    std::unordered_map<const IHavokObject*, std::string> smOfState;   // state obj -> owning SM name
    for (const auto& [obj, cat] : identity.category) {
        const auto* so = dynamic_cast<const io::SchemaObject*>(obj);
        if (!so || std::string(so->ClassName()) != "hkbStateMachine") continue;
        const std::string smName = fStr(*so, "name");
        if (const io::FieldValue* st = fieldByName(*so, "states"))
            for (const auto& s : st->objs) if (s) smOfState[s.get()] = smName;
    }
    std::unordered_map<std::string, std::string>         old2new;
    std::unordered_map<const IHavokObject*, std::string> newIds;
    for (const auto& [obj, oldId] : identity.ids) {
        const auto* so = dynamic_cast<const io::SchemaObject*>(obj);
        if (!so) continue;
        const std::string cls = so->ClassName();
        std::string eid;
        if (cls == "hkbStateMachineStateInfo") {
            auto it = smOfState.find(obj);
            eid = "hkbStateMachineStateInfo:" + (it != smOfState.end() ? it->second + "_" : std::string()) + fStr(*so, "name");
        } else {
            const std::string nm = fStr(*so, "name");
            if (nm.empty()) continue;   // nameless / inline: keep #NNNN (folded to owner, never a ref target)
            eid = cls + ":" + nm;
        }
        old2new[oldId] = eid;
        newIds[obj]    = eid;
    }
    for (const auto& [obj, eid] : newIds) identity.ids[obj] = eid;
    std::set<std::string> nd;
    for (const auto& d : deltaIds) { auto it = old2new.find(d); nd.insert(it != old2new.end() ? it->second : d); }
    deltaIds.swap(nd);
}

bool EmitHky(const Identity& identity, const schema::SchemaRegistry& /*reg*/,
             const std::string& outDir, std::string& err, const std::set<std::string>* deltaIds,
             std::vector<std::string>* warnings) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const NameResolver nr = buildResolver(identity);
    int nodeSeq = 0;   // numeric per-emit filename counter — identity is the id: field, not the filename
                       // (a (class,name) editorId contains ':' and spaces, illegal/awkward in a filename)

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
            const std::string fn = std::to_string(nodeSeq++);
            std::ofstream of(dir / (fn + ".yaml"), std::ios::binary);
            of << "id: " << id << "\n" << y;
            // A modifier that says `<field>: null` for an owned data array MUST ship the array as a
            // data/<base>_<suffix>.yaml sidecar, or the runtime re-link leaves it null and Havok crashes.
            emitModifierSidecars(*so, id, fn, fs::path(outDir), nr);
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
        std::ofstream of(dir / (std::to_string(nodeSeq++) + ".yaml"), std::ios::binary);
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
    namespace en = havok::model::enums;
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

    std::string y;
    // Added variables (name, type, initial value; quadValue for vector/quaternion).
    { bool hdr = false;
      for (std::size_t i = 0; i < mVar.size(); ++i) {
        if (bVarS.count(mVar[i])) continue;
        if (!hdr) { y += "variables:\n"; hdr = true; }
        y += "  - name: " + q(mVar[i]) + "\n";
        long type = 0; if (const auto* vi = elemAt(varInfos, i)) type = decodeInt(fieldByName(*vi, "type")->raw, Scalar::Int8);
        const std::string typeStr = revNum(en::VariableType(), type);
        y += "    type: " + typeStr + "\n";
        long val = 0; if (const auto* wv = elemAt(wordVals, i)) val = decodeInt(fieldByName(*wv, "value")->raw, Scalar::Int32);
        y += "    value: " + std::to_string(val) + "\n";
        if ((typeStr == "VARIABLE_TYPE_VECTOR4" || typeStr == "VARIABLE_TYPE_QUATERNION" || typeStr == "VARIABLE_TYPE_VECTOR3")
            && quadVals && val >= 0 && static_cast<std::size_t>(val) * 16 + 16 <= quadVals->raw.size())
            y += "    quadValue: " + pvecRaw(std::vector<std::uint8_t>(quadVals->raw.begin() + val * 16, quadVals->raw.begin() + val * 16 + 16)) + "\n";
      }
    }
    // Added events (name, flags).
    { bool hdr = false;
      for (std::size_t i = 0; i < mEv.size(); ++i) {
        if (bEvS.count(mEv[i])) continue;
        if (!hdr) { y += "events:\n"; hdr = true; }
        y += "  - name: " + q(mEv[i]) + "\n";
        long flags = 0; if (const auto* ei = elemAt(evInfos, i)) flags = decodeInt(fieldByName(*ei, "flags")->raw, Scalar::UInt32);
        y += "    flags: " + en::FormatFlags(flags, en::EventInfoFlags()) + "\n";
      }
    }
    // Added character properties (name, type, role flags).
    { bool hdr = false;
      for (std::size_t i = 0; i < mCp.size(); ++i) {
        if (bCpS.count(mCp[i])) continue;
        if (!hdr) { y += "characterPropertyNames:\n"; hdr = true; }
        y += "  - name: " + q(mCp[i]) + "\n";
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

    if (y.empty()) return true;   // no added vocabulary
    std::error_code ec; fs::create_directories(fs::path(outDir) / "data", ec);
    std::ofstream of(fs::path(outDir) / "data" / "additive.yaml", std::ios::binary);
    of << y;
    if (!of) { err = "EmitAdditiveVocab: cannot write additive.yaml"; return false; }
    return true;
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
namespace en = havok::model::enums;
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
// (havok::cross::parseVec4) — the ONE vec4 text codec, also used by havok-core BehaviorBuilder::pv4.
// It accepts both the decompiler's "(x y z w)" and the bare/multi-line float form, so BR-28 (a bare
// axisOfRotation reading as 0 0 0 0) can't recur and the two paths can't diverge (B4 collapsed).
std::vector<std::uint8_t> parseVec4Raw(const std::string& t) {
    const auto q = havok::cross::parseVec4(t);
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
const havok::xml::Node* paramByName(const havok::xml::Node& obj, const std::string& name) {
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
void fillObject(io::SchemaObject& so, const havok::xml::Node& node,
                const schema::SchemaRegistry& reg,
                const std::unordered_map<std::string, std::shared_ptr<io::SchemaObject>>& idMap,
                const SymTab* syms, std::string& err) {
    const std::string cls = so.ClassName();
    const auto fields = so.Fields();
    for (std::size_t i = 0; i < fields.size(); ++i) {
        const Field& f = *fields[i];
        if (f.name.empty() || f.ignored) continue;         // vtable/skip/pad + SERIALIZE_IGNORED: no hkparam
        const havok::xml::Node* p = paramByName(node, f.name);
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
                if (const havok::xml::Node* inner = p->child("hkobject")) {
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
void collectTopLevel(const havok::xml::Node& n, std::vector<const havok::xml::Node*>& out) {
    if (n.tag == "hkobject") {
        const auto nm = n.attr("name");
        if (!nm.empty() && nm.front() == '#') { out.push_back(&n); return; }   // don't descend into a named obj
    }
    for (const auto& c : n.children) collectTopLevel(c, out);
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
    std::vector<havok::xml::Node> roots; roots.reserve(sources.size());

    // Per CANONICAL id, the ORDERED layers that define it (base first, then each overriding patch). Each
    // layer records whether it is a delta and, for a delta, the fields it ACTUALLY changed (its MOD_CODE
    // delta, via the shared changedFields). The merge is NO LONGER a field-wise last-writer overlay —
    // every id's layers go through the SHARED havok::merge::bashMerge, so an array field edited by 2+
    // mods (variableNames/eventNames rosters, clip triggers, state/transition lists) is UNIONED exactly
    // as the runtime YAML loader does. Field-wise last-writer here silently dropped all-but-the-last
    // mod's array additions — the TDM_Pitch A-pose when one mod's several Nemesis codes are merged into
    // one bundle. New-node ids (#code$N) live in the same map as base #NNNN, so refs resolve either way.
    struct Layer { const havok::xml::Node* node; bool isDelta; const std::unordered_set<std::string>* changed; };
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
            changedHold.push_back(std::make_unique<std::unordered_set<std::string>>(havok::merge::changedFields(*text)));
            changedPtr = changedHold.back().get();
            havok::xml::StripPatchOriginals(parseText);
        }
        roots.push_back(havok::xml::Parse("<r>" + parseText + "</r>"));
        std::vector<const havok::xml::Node*> nodes;
        collectTopLevel(roots.back(), nodes);
        for (const havok::xml::Node* n : nodes) {
            const std::string id = canonId(std::string(n->attr("name")));
            if (layers.find(id) == layers.end()) order.push_back(id);
            layers[id].push_back({ n, isDelta, changedPtr });
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
    std::unordered_map<std::string, havok::xml::Node> mergedById;
    mergedById.reserve(order.size());
    for (const std::string& id : order) {
        const std::vector<Layer>& ls = layers[id];
        const std::string objCls(idMap[id]->ClassName());
        const havok::xml::Node* baseNode = nullptr;
        for (const Layer& L : ls) if (!L.isDelta) baseNode = L.node;   // the base node (normally exactly one)
        std::size_t seedSkip = 0;
        if (!baseNode) { baseNode = ls.front().node; seedSkip = 1; }   // new node: first layer seeds the merge

        std::vector<havok::merge::PatchLayer> pls; pls.reserve(ls.size());
        std::size_t seen = 0;
        for (const Layer& L : ls) {
            const bool isSeed = (seedSkip == 1 && seen++ == 0);
            if (!L.isDelta || isSeed) continue;
            if (std::string(L.node->attr("class")) != objCls) continue;   // source drift — skip this layer
            pls.push_back({ *L.node, L.changed ? *L.changed : std::unordered_set<std::string>{} });
        }
        std::vector<const havok::merge::PatchLayer*> ptrs; ptrs.reserve(pls.size());
        for (const auto& p : pls) ptrs.push_back(&p);
        // compose predicate: read the field's `merge:` tag from the SAME SchemaRegistry the runtime
        // merge reads (SchemaRegistry::MergeTag) — so converter and runtime compose identically.
        auto composePred = [&reg](const std::string& c, const std::string& f) {
            return reg.MergeTag(c, f) == "compose";
        };
        mergedById.emplace(id, ptrs.empty() ? *baseNode
                               : havok::merge::bashMerge(*baseNode, ptrs, /*guarded*/ {}, composePred));
    }

    // Nemesis symbol roster: variable/event NAME -> merged index, from the MERGED hkbBehaviorGraphStringData
    // (now the UNIONED roster across all mods, so a name only one mod added still resolves). Bindings /
    // transitions written as $variableID[Name]$ / $eventID[Name]$ resolve against it in pass 2. First
    // string-data wins (the graph's own). Built BEFORE pass 2 so it's ready when a binding field is parsed.
    SymTab syms;
    for (const std::string& id : order) {
        const havok::xml::Node& n = mergedById[id];
        if (std::string(n.attr("class")) != "hkbBehaviorGraphStringData") continue;
        auto fill = [&](const char* param, std::unordered_map<std::string, int>& m) {
            if (const havok::xml::Node* p = paramByName(n, param)) {
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
                               const schema::SchemaRegistry& reg, const std::string& outDeltaDir) {
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
    // Speak the base master's (class,name) editorID identity so the delta merges by stable name, not by
    // a colliding tagfile number. Remaps merged.identity.ids AND the delta-id set in lockstep.
    RemapToEditorIds(merged.identity, ds);
    if (!EmitHky(merged.identity, reg, outDeltaDir, err, &ds, &r.warnings)) { r.error = "emit: " + err; return r; }

    // Added-vocabulary sidecar — diff the base graph vocab against the merged graph's.
    ParsedTagfile baseParsed;
    if (ParseTagfile(baseTagfileXml, reg, baseParsed, err))
        if (!EmitAdditiveVocab(baseParsed.identity, merged.identity, outDeltaDir, err)) { r.error = "additive: " + err; return r; }

    r.ok = true;
    return r;
}

} // namespace havok::model
