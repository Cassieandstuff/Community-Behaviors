// SchemaBuilder — the schema-driven compile direction (Migration M2+): Def -> generic SchemaObject,
// serialized by havok-io. The replacement for havok-core's typed BehaviorBuilder (which constructs
// hk* C++ objects). Grown node-by-node, each gated byte-identical against the typed compile.

#include <havok-model/HavokModel.h>

#include "havok/model/defs/GeneratorDefs.h"   // ClipGeneratorDef (the doc lives in havok-model since 3r.1)
#include "havok/model/defs/CommonDefs.h"       // ClipTriggerDef, BindingDef
#include "havok/model/defs/StateMachineDefs.h" // StateMachineDef, StateDef, TransitionInfoDef, EventPropertyDef
#include "havok/model/defs/ModifierDefs.h"     // modifier node Defs
#include "havok/model/defs/BehaviorDef.h"      // BehaviorDef, BehaviorGraphDataDef (graph assembler)
#include "havok/model/BehaviorData.h"          // the merged name-keyed doc (AssembleGraph input)
#include "havok/model/ProjectData.h"           // ProjectSpec (AssembleProject input)
#include "havok/model/defs/CharacterDefs.h"    // CharacterData (AssembleCharacter input)
#include "havok/model/HavokEnums.h"            // enums::ResolveEnum / PlaybackMode / BindingType (havok-framing)

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

namespace havok::model {
namespace {

// Little-endian byte encodings, matching the typed serializer (BinaryWriterEx writes native LE).
std::vector<std::uint8_t> leBytes(std::uint64_t v, int n) {
    std::vector<std::uint8_t> b(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) b[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>((v >> (8 * i)) & 0xFF);
    return b;
}

// Float string -> 4 raw LE bytes. `pf` in the typed builder is std::stof (empty/parse-fail -> 0), so
// this reproduces the exact float bits the typed path emits.
std::vector<std::uint8_t> f32(const std::string& s) {
    float v = 0.f;
    if (!s.empty()) { try { v = std::stof(s); } catch (...) { v = 0.f; } }
    std::vector<std::uint8_t> b(4);
    std::memcpy(b.data(), &v, 4);
    return b;
}
bool parseBool(const std::string& s) { return s=="true" || s=="True" || s=="1"; }

// Parse a "(x y z w)" vector/quaternion literal → 16 raw LE bytes (4 float32). Matches the typed pv4/pq4
// EXACTLY: direct istream float extraction, so an unparseable token (e.g. vanilla runtime "-nan(ind)"
// garbage in error/mark fields) leaves 0 (C++11 >> zeros on failure) rather than stof's NaN bit pattern.
std::vector<std::uint8_t> vec4Bytes(const std::string& s) {
    float v[4] = {0,0,0,0};
    std::string t = s;
    for (char& c : t) if (c=='(' || c==')' || c==',') c = ' ';
    std::istringstream is(t);
    is >> v[0] >> v[1] >> v[2] >> v[3];
    std::vector<std::uint8_t> b(16);
    std::memcpy(b.data(), v, 16);
    return b;
}

// New default-initialized SchemaObject for `cls`, or nullptr if the schema isn't registered.
std::shared_ptr<io::SchemaObject> make(const schema::SchemaRegistry& reg, const char* cls) {
    const schema::ClassSchema* cs = reg.Find(cls);
    if (!cs) return nullptr;
    auto o = std::make_shared<io::SchemaObject>(&reg, cs);
    o->Init();
    return o;
}

// hkbStringEventPayload{ data } — the only payload kind on this path; null for empty/"null".
std::shared_ptr<io::SchemaObject> buildPayload(const std::string& payload, const schema::SchemaRegistry& reg) {
    if (payload.empty() || payload == "null") return nullptr;
    auto p = make(reg, "hkbStringEventPayload");
    if (p) p->FieldRef("data").str = payload;
    return p;
}

// Fill an INLINE hkbEventProperty (already created by Init as ev.event.obj): id + optional payload.
// Post the Stage-4 bindings-resolve pass the event name is cleared and `id` is the resolved index.
void fillEvent(io::SchemaObject& ev, int id, const std::string& payload, const schema::SchemaRegistry& reg) {
    ev.FieldRef("id").raw = leBytes(static_cast<std::uint64_t>(static_cast<std::uint32_t>(id)), 4);
    if (auto p = buildPayload(payload, reg)) ev.FieldRef("payload").obj = p;
}

} // namespace

// hkbClipTriggerArray{ triggers[] } — one hkbClipTrigger per ClipTriggerDef (mirrors buildTriggers).
std::shared_ptr<io::SchemaObject> BuildTriggers(const std::vector<ClipTriggerDef>& triggers,
                                                const schema::SchemaRegistry& reg) {
    if (triggers.empty()) return nullptr;
    auto arr = make(reg, "hkbClipTriggerArray");
    if (!arr) return nullptr;
    auto& objs = arr->FieldRef("triggers").objs;
    for (const auto& t : triggers) {
        auto tr = make(reg, "hkbClipTrigger");
        if (!tr) continue;
        tr->FieldRef("localTime").raw          = f32(t.localTime);
        tr->FieldRef("relativeToEndOfClip").raw = leBytes(t.relativeToEndOfClip ? 1u : 0u, 1);
        tr->FieldRef("acyclic").raw            = leBytes(t.acyclic ? 1u : 0u, 1);
        tr->FieldRef("isAnnotation").raw       = leBytes(t.isAnnotation ? 1u : 0u, 1);
        if (auto ev = std::dynamic_pointer_cast<io::SchemaObject>(tr->FieldRef("event").obj))
            fillEvent(*ev, t.eventId, t.payload, reg);
        objs.push_back(tr);
    }
    return arr;
}

// hkbVariableBindingSet{ bindings[], indexOfBindingToEnable } (mirrors buildBindingSet). Post the
// Stage-4 pass, each binding's `variable` name is cleared and `variableIndex` is the resolved index.
std::shared_ptr<io::SchemaObject> BuildBindingSet(const std::vector<BindingDef>& bindings,
                                                  const schema::SchemaRegistry& reg) {
    if (bindings.empty()) return nullptr;
    auto set = make(reg, "hkbVariableBindingSet");
    if (!set) return nullptr;
    auto& objs = set->FieldRef("bindings").objs;
    int enableIndex = -1;
    for (int i = 0; i < static_cast<int>(bindings.size()); ++i) {
        const auto& b = bindings[static_cast<std::size_t>(i)];
        auto bd = make(reg, "hkbVariableBindingSetBinding");
        if (!bd) continue;
        bd->FieldRef("memberPath").str    = b.memberPath;
        bd->FieldRef("variableIndex").raw = leBytes(static_cast<std::uint64_t>(static_cast<std::uint32_t>(b.variableIndex)), 4);
        bd->FieldRef("bitIndex").raw      = leBytes(static_cast<std::uint64_t>(static_cast<std::uint8_t>(static_cast<std::int8_t>(b.bitIndex))), 1);
        bd->FieldRef("bindingType").raw   = leBytes(static_cast<std::uint64_t>(enums::ResolveEnum(b.bindingType, enums::BindingType())), 1);
        if (b.enableTarget) enableIndex = i;
        objs.push_back(bd);
    }
    set->FieldRef("indexOfBindingToEnable").raw = leBytes(static_cast<std::uint64_t>(static_cast<std::uint32_t>(enableIndex)), 4);
    return set;
}

// hkbBoneWeightArray{ boneWeights: float[] } — a dense scalararray<float>, one weight per skeleton bone
// in bone-order. Two author-facing sources, both emitting the identical LE-float payload:
//   • RAW   (count + space-separated floats) — vanilla / decompiled data, already in skeleton order.
//   • NAMED (bone name -> weight, arbitrary order + a `default` fill) — the author writes only the bones
//     that differ from the default; the compiler places each at its slot in `boneNames` (the target
//     skeleton's bone list). This is the compatibility layer: the name is the identity, the index is a
//     skeleton-relative coordinate, so the same authored map compiles against vanilla or a custom skeleton.
//     Mirrors BuildBoneIndexArray — the name->index scan is the same, only the payload (float vs int16) differs.
std::shared_ptr<io::SchemaObject> BuildBoneWeights(const BoneWeightsDef& bw, const schema::SchemaRegistry& reg,
                                                   const std::vector<std::string>& boneNames) {
    auto arr = make(reg, "hkbBoneWeightArray");
    if (!arr) return nullptr;
    std::vector<std::uint8_t>& raw = arr->FieldRef("boneWeights").raw;
    auto emit = [&](float f) { std::uint8_t b[4]; std::memcpy(b, &f, 4); raw.insert(raw.end(), b, b + 4); };
    auto toF  = [](const std::string& s) { try { return std::stof(s); } catch (...) { return 0.f; } };

    if (bw.IsNamed() && !boneNames.empty()) {
        // Dense array over the target skeleton's bone order: `default` everywhere, named bones override.
        const std::size_t n = bw.boneCount ? static_cast<std::size_t>(*bw.boneCount) : boneNames.size();
        std::vector<float> w(n, toF(bw.defaultWeight));
        for (const auto& [name, valStr] : *bw.named) {
            int idx = -1;
            for (int i = 0; i < static_cast<int>(boneNames.size()); ++i)
                if (boneNames[static_cast<std::size_t>(i)] == name) { idx = i; break; }
            if (idx < 0 || static_cast<std::size_t>(idx) >= n) continue;   // bone absent from target skeleton — skip
            w[static_cast<std::size_t>(idx)] = toF(valStr);
        }
        for (float f : w) emit(f);
        return arr;
    }

    if (bw.count > 0 && !bw.values.empty()) {   // raw / decompiled path (already in skeleton order)
        std::stringstream ss(bw.values);
        float f;
        while (ss >> f) emit(f);
    }
    return arr;
}

// One hkbBlenderGeneratorChild: generator edge (resolved), owned boneWeights + bindings, weights.
std::shared_ptr<io::SchemaObject> BuildBlenderChild(const BlenderChildDef& c, const schema::SchemaRegistry& reg,
                                                    const GenResolver& resolve, const std::vector<std::string>& boneNames) {
    auto child = make(reg, "hkbBlenderGeneratorChild");
    if (!child) return nullptr;
    if (!c.generator.empty() && c.generator != "null")
        if (auto g = resolve(c.generator)) child->FieldRef("generator").obj = g;
    if (c.boneWeights) { if (auto bwa = BuildBoneWeights(*c.boneWeights, reg, boneNames)) child->FieldRef("boneWeights").obj = bwa; }
    child->FieldRef("weight").raw               = f32(c.weight);
    child->FieldRef("worldFromModelWeight").raw = f32(c.worldFromModelWeight);
    if (c.bindings) { if (auto b = BuildBindingSet(*c.bindings, reg)) child->FieldRef("variableBindingSet").obj = b; }
    return child;
}

std::shared_ptr<io::SchemaObject> BuildBlender(const BlenderGeneratorDef& def, const schema::SchemaRegistry& reg,
                                               const GenResolver& resolve, const std::vector<std::string>& boneNames) {
    auto o = make(reg, "hkbBlenderGenerator");
    if (!o) return nullptr;
    o->FieldRef("userData").raw = leBytes(static_cast<std::uint64_t>(def.userData), 8);
    o->FieldRef("name").str     = def.name;
    o->FieldRef("referencePoseWeightThreshold").raw = f32(def.referencePoseWeightThreshold);
    o->FieldRef("blendParameter").raw               = f32(def.blendParameter);
    o->FieldRef("minCyclicBlendParameter").raw      = f32(def.minCyclicBlendParameter);
    o->FieldRef("maxCyclicBlendParameter").raw      = f32(def.maxCyclicBlendParameter);
    o->FieldRef("indexOfSyncMasterChild").raw = leBytes(static_cast<std::uint64_t>(static_cast<std::uint16_t>(static_cast<std::int16_t>(def.indexOfSyncMasterChild))), 2);
    o->FieldRef("flags").raw                  = leBytes(static_cast<std::uint64_t>(static_cast<std::uint16_t>(static_cast<std::int16_t>(def.flags))), 2);
    o->FieldRef("subtractLastChild").raw      = leBytes(def.subtractLastChild ? 1u : 0u, 1);
    if (def.bindings) { if (auto b = BuildBindingSet(*def.bindings, reg)) o->FieldRef("variableBindingSet").obj = b; }
    auto& kids = o->FieldRef("children").objs;
    for (const auto& c : def.children) {
        if (c.generator.empty() || c.generator == "null") continue;   // mirror emitter/typed builder
        if (auto ch = BuildBlenderChild(c, reg, resolve, boneNames)) kids.push_back(ch);
    }
    return o;
}

// ── state-machine family ──────────────────────────────────────────────────────
namespace {
std::vector<std::uint8_t> i32(int v) { return leBytes(static_cast<std::uint64_t>(static_cast<std::uint32_t>(v)), 4); }
std::vector<std::uint8_t> i16(int v) { return leBytes(static_cast<std::uint64_t>(static_cast<std::uint16_t>(static_cast<std::int16_t>(v))), 2); }

// hkbExpressionCondition / hkbStringCondition (the transition condition sub-node), else null.
std::shared_ptr<io::SchemaObject> buildCondition(const std::optional<std::string>& cond,
                                                 const std::optional<std::string>& condStr,
                                                 const schema::SchemaRegistry& reg) {
    if (cond && *cond != "null")    { auto c = make(reg,"hkbExpressionCondition"); if(c) c->FieldRef("expression").str = *cond; return c; }
    if (condStr && *condStr != "null") { auto c = make(reg,"hkbStringCondition"); if(c) c->FieldRef("conditionString").str = *condStr; return c; }
    return nullptr;
}
} // namespace

// hkbStateMachineEventPropertyArray{ events[]: hkbEventProperty{id + payload} }.
std::shared_ptr<io::SchemaObject> BuildEventArray(const std::vector<EventPropertyDef>& events, const schema::SchemaRegistry& reg) {
    if (events.empty()) return nullptr;
    auto arr = make(reg, "hkbStateMachineEventPropertyArray");
    if (!arr) return nullptr;
    auto& objs = arr->FieldRef("events").objs;
    for (const auto& e : events) { auto ep = make(reg, "hkbEventProperty"); if (!ep) continue; fillEvent(*ep, e.id, e.payload, reg); objs.push_back(ep); }
    return arr;
}

// hkbStateMachineTransitionInfoArray{ transitions[]: hkbStateMachineTransitionInfo }.
std::shared_ptr<io::SchemaObject> BuildTransitions(const std::vector<TransitionInfoDef>& transitions,
                                                   const schema::SchemaRegistry& reg, const GenResolver& resolve) {
    if (transitions.empty()) return nullptr;
    auto arr = make(reg, "hkbStateMachineTransitionInfoArray");
    if (!arr) return nullptr;
    auto& objs = arr->FieldRef("transitions").objs;
    for (const auto& t : transitions) {
        auto ti = make(reg, "hkbStateMachineTransitionInfo");
        if (!ti) continue;
        auto setInterval = [&](const char* field, const TransitionIntervalDef& iv) {
            if (auto s = std::dynamic_pointer_cast<io::SchemaObject>(ti->FieldRef(field).obj)) {
                s->FieldRef("enterEventId").raw = i32(iv.enterEventId);
                s->FieldRef("exitEventId").raw  = i32(iv.exitEventId);
                s->FieldRef("enterTime").raw    = f32(iv.enterTime);
                s->FieldRef("exitTime").raw     = f32(iv.exitTime);
            }
        };
        setInterval("triggerInterval",  t.triggerInterval);
        setInterval("initiateInterval", t.initiateInterval);
        if (!t.transition.empty() && t.transition != "null") if (auto te = resolve(t.transition)) ti->FieldRef("transition").obj = te;
        if (auto c = buildCondition(t.condition, t.conditionString, reg)) ti->FieldRef("condition").obj = c;
        ti->FieldRef("eventId").raw           = i32(t.eventId);
        ti->FieldRef("toStateId").raw         = i32(t.toStateId);
        ti->FieldRef("fromNestedStateId").raw = i32(t.fromNestedStateId);
        ti->FieldRef("toNestedStateId").raw   = i32(t.toNestedStateId);
        ti->FieldRef("priority").raw          = i16(t.priority);
        ti->FieldRef("flags").raw             = i16(static_cast<int>(enums::ResolveEnum(t.flags, enums::TransitionFlags())));
        objs.push_back(ti);
    }
    return arr;
}

// hkbStateMachineStateInfo — generator edge + owned notify-event arrays + transitions + bindings.
std::shared_ptr<io::SchemaObject> BuildState(const StateDef& def, const schema::SchemaRegistry& reg, const GenResolver& resolve) {
    auto o = make(reg, "hkbStateMachineStateInfo");
    if (!o) return nullptr;
    o->FieldRef("name").str        = def.name;
    o->FieldRef("stateId").raw     = i32(def.stateId);
    o->FieldRef("probability").raw = f32(def.probability);
    o->FieldRef("enable").raw      = leBytes(def.enable ? 1u : 0u, 1);
    if (!def.generator.empty() && def.generator != "null") if (auto g = resolve(def.generator)) o->FieldRef("generator").obj = g;
    if (def.enterNotifyEvents) { if (auto a = BuildEventArray(*def.enterNotifyEvents, reg)) o->FieldRef("enterNotifyEvents").obj = a; }
    if (def.exitNotifyEvents)  { if (auto a = BuildEventArray(*def.exitNotifyEvents, reg))  o->FieldRef("exitNotifyEvents").obj  = a; }
    if (def.parsedTransitions) { if (auto a = BuildTransitions(*def.parsedTransitions, reg, resolve)) o->FieldRef("transitions").obj = a; }
    if (def.bindings) { if (auto b = BuildBindingSet(*def.bindings, reg)) o->FieldRef("variableBindingSet").obj = b; }
    return o;
}

std::shared_ptr<io::SchemaObject> BuildStateMachine(const StateMachineDef& def, const schema::SchemaRegistry& reg, const GenResolver& resolve) {
    auto o = make(reg, "hkbStateMachine");
    if (!o) return nullptr;
    o->FieldRef("userData").raw = leBytes(static_cast<std::uint64_t>(def.userData), 8);
    o->FieldRef("name").str     = def.name;
    if (auto ev = std::dynamic_pointer_cast<io::SchemaObject>(o->FieldRef("eventToSendWhenStateOrTransitionChanges").obj))
        ev->FieldRef("id").raw = i32(def.eventToSendWhenStateOrTransitionChangesId);
    o->FieldRef("startStateId").raw                       = i32(def.startStateId);
    o->FieldRef("returnToPreviousStateEventId").raw        = i32(def.returnToPreviousStateEventId);
    o->FieldRef("randomTransitionEventId").raw            = i32(def.randomTransitionEventId);
    o->FieldRef("transitionToNextHigherStateEventId").raw = i32(def.transitionToNextHigherStateEventId);
    o->FieldRef("transitionToNextLowerStateEventId").raw  = i32(def.transitionToNextLowerStateEventId);
    o->FieldRef("syncVariableIndex").raw                  = i32(def.syncVariableIndex);
    o->FieldRef("wrapAroundStateId").raw                  = leBytes(def.wrapAroundStateId ? 1u : 0u, 1);
    o->FieldRef("maxSimultaneousTransitions").raw = leBytes(static_cast<std::uint64_t>(static_cast<std::uint8_t>(static_cast<std::int8_t>(def.maxSimultaneousTransitions))), 1);
    o->FieldRef("startStateMode").raw     = leBytes(static_cast<std::uint64_t>(enums::ResolveEnum(def.startStateMode, enums::StartStateMode())), 1);
    o->FieldRef("selfTransitionMode").raw = leBytes(static_cast<std::uint64_t>(enums::ResolveEnum(def.selfTransitionMode, enums::SmSelfTransitionMode())), 1);
    if (def.bindings) { if (auto b = BuildBindingSet(*def.bindings, reg)) o->FieldRef("variableBindingSet").obj = b; }
    auto& sts = o->FieldRef("states").objs;
    for (const auto& s : def.states) if (auto n = resolve(s)) sts.push_back(n);
    if (def.parsedWildcardTransitions && !def.parsedWildcardTransitions->empty())
        if (auto wt = BuildTransitions(*def.parsedWildcardTransitions, reg, resolve)) o->FieldRef("wildcardTransitions").obj = wt;
    return o;
}

// ── modifier family (batch 1: leaf + edge-only) ───────────────────────────────
namespace {
std::vector<std::uint8_t> b1(bool v) { return leBytes(v ? 1u : 0u, 1); }
// hkbNode base (userData/name) + optional hkbModifier `enable` + bindings — the common modifier prefix.
void modBase(io::SchemaObject& o, const std::string& name, int userData, const schema::SchemaRegistry& reg,
             const std::optional<std::vector<BindingDef>>& bindings, const bool* enable = nullptr) {
    o.FieldRef("userData").raw = leBytes(static_cast<std::uint64_t>(userData), 8);
    o.FieldRef("name").str     = name;
    if (enable && o.HasField("enable")) o.FieldRef("enable").raw = b1(*enable);
    if (bindings) { if (auto b = BuildBindingSet(*bindings, reg)) o.FieldRef("variableBindingSet").obj = b; }
}
} // namespace

// hkbModifierGenerator — wraps a modifier applied to a child generator (base hkbGenerator: no `enable`).
std::shared_ptr<io::SchemaObject> BuildModifierGenerator(const ModifierGeneratorDef& def, const schema::SchemaRegistry& reg, const GenResolver& resolve) {
    auto o = make(reg, "hkbModifierGenerator"); if (!o) return nullptr;
    modBase(*o, def.name, def.userData, reg, def.bindings);
    if (!def.modifier.empty()  && def.modifier  != "null") if (auto m = resolve(def.modifier))  o->FieldRef("modifier").obj  = m;
    if (!def.generator.empty() && def.generator != "null") if (auto g = resolve(def.generator)) o->FieldRef("generator").obj = g;
    return o;
}

// hkbModifierList — ordered list of child modifiers (edges).
std::shared_ptr<io::SchemaObject> BuildModifierList(const ModifierListDef& def, const schema::SchemaRegistry& reg, const GenResolver& resolve) {
    auto o = make(reg, "hkbModifierList"); if (!o) return nullptr;
    modBase(*o, def.name, def.userData, reg, def.bindings, &def.enable);
    auto& objs = o->FieldRef("modifiers").objs;
    for (const auto& m : def.modifiers) { if (m.empty() || m == "null") continue; if (auto n = resolve(m)) objs.push_back(n); }
    return o;
}

// BSIsActiveModifier — 5 active/invert bool pairs.
std::shared_ptr<io::SchemaObject> BuildIsActiveModifier(const BSIsActiveModifierDef& def, const schema::SchemaRegistry& reg) {
    auto o = make(reg, "BSIsActiveModifier"); if (!o) return nullptr;
    modBase(*o, def.name, def.userData, reg, def.bindings, &def.enable);
    o->FieldRef("bIsActive0").raw=b1(def.bIsActive0); o->FieldRef("bInvertActive0").raw=b1(def.bInvertActive0);
    o->FieldRef("bIsActive1").raw=b1(def.bIsActive1); o->FieldRef("bInvertActive1").raw=b1(def.bInvertActive1);
    o->FieldRef("bIsActive2").raw=b1(def.bIsActive2); o->FieldRef("bInvertActive2").raw=b1(def.bInvertActive2);
    o->FieldRef("bIsActive3").raw=b1(def.bIsActive3); o->FieldRef("bInvertActive3").raw=b1(def.bInvertActive3);
    o->FieldRef("bIsActive4").raw=b1(def.bIsActive4); o->FieldRef("bInvertActive4").raw=b1(def.bInvertActive4);
    return o;
}

// hkbEventDrivenModifier — hkbModifierWrapper (single modifier edge) gated by activate/deactivate events.
std::shared_ptr<io::SchemaObject> BuildEventDrivenModifier(const EventDrivenModifierDef& def, const schema::SchemaRegistry& reg, const GenResolver& resolve) {
    auto o = make(reg, "hkbEventDrivenModifier"); if (!o) return nullptr;
    modBase(*o, def.name, def.userData, reg, def.bindings, &def.enable);
    if (!def.modifier.empty() && def.modifier != "null") if (auto m = resolve(def.modifier)) o->FieldRef("modifier").obj = m;
    o->FieldRef("activateEventId").raw   = leBytes(static_cast<std::uint64_t>(static_cast<std::uint32_t>(def.activateEventId)), 4);
    o->FieldRef("deactivateEventId").raw = leBytes(static_cast<std::uint64_t>(static_cast<std::uint32_t>(def.deactivateEventId)), 4);
    o->FieldRef("activeByDefault").raw   = b1(def.activeByDefault);
    return o;
}

// BSEventEveryNEventsModifier — inline eventToCheckFor / eventToSend + counters.
std::shared_ptr<io::SchemaObject> BuildEventEveryN(const BSEventEveryNEventsModifierDef& def, const schema::SchemaRegistry& reg) {
    auto o = make(reg, "BSEventEveryNEventsModifier"); if (!o) return nullptr;
    modBase(*o, def.name, def.userData, reg, def.bindings, &def.enable);
    if (auto e = std::dynamic_pointer_cast<io::SchemaObject>(o->FieldRef("eventToCheckFor").obj)) fillEvent(*e, def.eventToCheckFor.id, def.eventToCheckFor.payload.value_or("null"), reg);
    if (auto e = std::dynamic_pointer_cast<io::SchemaObject>(o->FieldRef("eventToSend").obj))     fillEvent(*e, def.eventToSend.id,     def.eventToSend.payload.value_or("null"), reg);
    o->FieldRef("numberOfEventsBeforeSend").raw        = leBytes(static_cast<std::uint64_t>(static_cast<std::uint8_t>(static_cast<std::int8_t>(def.numberOfEventsBeforeSend))), 1);
    o->FieldRef("minimumNumberOfEventsBeforeSend").raw = leBytes(static_cast<std::uint64_t>(static_cast<std::uint8_t>(static_cast<std::int8_t>(def.minimumNumberOfEventsBeforeSend))), 1);
    o->FieldRef("randomizeNumberOfEvents").raw         = b1(def.randomizeNumberOfEvents);
    return o;
}

// BSInterpValueModifier — scalar interp/damp (4 floats).
std::shared_ptr<io::SchemaObject> BuildInterpValue(const BSInterpValueModifierDef& def, const schema::SchemaRegistry& reg) {
    auto o = make(reg, "BSInterpValueModifier"); if (!o) return nullptr;
    modBase(*o, def.name, def.userData, reg, def.bindings, &def.enable);
    o->FieldRef("source").raw=f32(def.source); o->FieldRef("target").raw=f32(def.target);
    o->FieldRef("result").raw=f32(def.result); o->FieldRef("gain").raw=f32(def.gain);
    return o;
}

// BSOffsetAnimationGenerator — default generator + offset clip generator + 3 offset floats
// (base hkbGenerator: no `enable`; the mark/frame fields are runtime-ignored).
std::shared_ptr<io::SchemaObject> BuildOffsetAnim(const BSOffsetAnimationGeneratorDef& def, const schema::SchemaRegistry& reg, const GenResolver& resolve) {
    auto o = make(reg, "BSOffsetAnimationGenerator"); if (!o) return nullptr;
    modBase(*o, def.name, def.userData, reg, def.bindings);
    if (!def.pDefaultGenerator.empty()    && def.pDefaultGenerator    != "null") if (auto g = resolve(def.pDefaultGenerator))    o->FieldRef("pDefaultGenerator").obj    = g;
    if (!def.pOffsetClipGenerator.empty() && def.pOffsetClipGenerator != "null") if (auto g = resolve(def.pOffsetClipGenerator)) o->FieldRef("pOffsetClipGenerator").obj = g;
    o->FieldRef("fOffsetVariable").raw   = f32(def.fOffsetVariable);
    o->FieldRef("fOffsetRangeStart").raw = f32(def.fOffsetRangeStart);
    o->FieldRef("fOffsetRangeEnd").raw   = f32(def.fOffsetRangeEnd);
    return o;
}

// ── specialized modifiers (foot-IK, ragdoll controls, look-at, keyframe, iState) ──
namespace {
// Fill an inline hkbFootIkGains struct (12 floats) from a FootIkGainsDef.
void fillGains(io::SchemaObject& g, const FootIkGainsDef& d) {
    g.FieldRef("onOffGain").raw = f32(std::to_string(d.onOffGain));
    g.FieldRef("groundAscendingGain").raw = f32(std::to_string(d.groundAscendingGain));
    g.FieldRef("groundDescendingGain").raw = f32(std::to_string(d.groundDescendingGain));
    g.FieldRef("footPlantedGain").raw = f32(std::to_string(d.footPlantedGain));
    g.FieldRef("footRaisedGain").raw = f32(std::to_string(d.footRaisedGain));
    g.FieldRef("footUnlockGain").raw = f32(std::to_string(d.footUnlockGain));
    g.FieldRef("worldFromModelFeedbackGain").raw = f32(std::to_string(d.worldFromModelFeedbackGain));
    g.FieldRef("errorUpDownBias").raw = f32(std::to_string(d.errorUpDownBias));
    g.FieldRef("alignWorldFromModelGain").raw = f32(std::to_string(d.alignWorldFromModelGain));
    g.FieldRef("hipOrientationGain").raw = f32(std::to_string(d.hipOrientationGain));
    g.FieldRef("maxKneeAngleDifference").raw = f32(std::to_string(d.maxKneeAngleDifference));
    g.FieldRef("ankleOrientationGain").raw = f32(std::to_string(d.ankleOrientationGain));
}
std::vector<std::uint8_t> ff(float v) { return f32(std::to_string(v)); }
} // namespace

// hkbFootIkControlsModifier — controlData.gains + per-leg data + error/align vectors.
std::shared_ptr<io::SchemaObject> BuildFootIkControls(const FootIkControlsModifierDef& def, const schema::SchemaRegistry& reg) {
    auto o = make(reg, "hkbFootIkControlsModifier"); if (!o) return nullptr;
    modBase(*o, def.name, def.userData, reg, def.bindings, &def.enable);
    if (auto cd = std::dynamic_pointer_cast<io::SchemaObject>(o->FieldRef("controlData").obj))
        if (auto g = std::dynamic_pointer_cast<io::SchemaObject>(cd->FieldRef("gains").obj)) fillGains(*g, def.controlData.gains);
    if (def.legs) { auto& objs = o->FieldRef("legs").objs;
        for (const auto& l : *def.legs) {
            auto lo = make(reg, "hkbFootIkControlsModifierLeg"); if (!lo) continue;
            lo->FieldRef("groundPosition").raw = vec4Bytes(l.groundPosition);
            if (l.ungroundedEvent) if (auto e = std::dynamic_pointer_cast<io::SchemaObject>(lo->FieldRef("ungroundedEvent").obj)) fillEvent(*e, l.ungroundedEvent->id, l.ungroundedEvent->payload.value_or("null"), reg);
            lo->FieldRef("verticalError").raw = ff(l.verticalError);
            lo->FieldRef("hitSomething").raw = leBytes(l.hitSomething ? 1u : 0u, 1);
            lo->FieldRef("isPlantedMS").raw = leBytes(l.isPlantedMS ? 1u : 0u, 1);
            objs.push_back(lo);
        } }
    o->FieldRef("errorOutTranslation").raw     = vec4Bytes(def.errorOutTranslation);
    o->FieldRef("alignWithGroundRotation").raw = vec4Bytes(def.alignWithGroundRotation);
    return o;
}

// hkbFootIkModifier — gains + per-leg ankle-placement data + many scalars/vectors.
std::shared_ptr<io::SchemaObject> BuildFootIkModifier(const FootIkModifierDef& def, const schema::SchemaRegistry& reg) {
    auto o = make(reg, "hkbFootIkModifier"); if (!o) return nullptr;
    modBase(*o, def.name, def.userData, reg, def.bindings, &def.enable);
    if (auto g = std::dynamic_pointer_cast<io::SchemaObject>(o->FieldRef("gains").obj)) fillGains(*g, def.gains);
    { auto& objs = o->FieldRef("legs").objs;
      for (const auto& l : def.legs) {
          auto lo = make(reg, "hkbFootIkModifierLeg"); if (!lo) continue;
          lo->FieldRef("prevAnkleRotLS").raw = vec4Bytes(l.prevAnkleRotLS);
          lo->FieldRef("kneeAxisLS").raw = vec4Bytes(l.kneeAxisLS);
          lo->FieldRef("footEndLS").raw = vec4Bytes(l.footEndLS);
          if (l.ungroundedEvent) if (auto e = std::dynamic_pointer_cast<io::SchemaObject>(lo->FieldRef("ungroundedEvent").obj)) fillEvent(*e, l.ungroundedEvent->id, l.ungroundedEvent->payload.value_or("null"), reg);
          lo->FieldRef("footPlantedAnkleHeightMS").raw = ff(l.footPlantedAnkleHeightMS);
          lo->FieldRef("footRaisedAnkleHeightMS").raw = ff(l.footRaisedAnkleHeightMS);
          lo->FieldRef("maxAnkleHeightMS").raw = ff(l.maxAnkleHeightMS);
          lo->FieldRef("minAnkleHeightMS").raw = ff(l.minAnkleHeightMS);
          lo->FieldRef("maxKneeAngleDegrees").raw = ff(l.maxKneeAngleDegrees);
          lo->FieldRef("minKneeAngleDegrees").raw = ff(l.minKneeAngleDegrees);
          lo->FieldRef("verticalError").raw = ff(l.verticalError);
          lo->FieldRef("maxAnkleAngleDegrees").raw = ff(l.maxAnkleAngleDegrees);
          lo->FieldRef("hipIndex").raw = i16(l.hipIndex);
          lo->FieldRef("kneeIndex").raw = i16(l.kneeIndex);
          lo->FieldRef("ankleIndex").raw = i16(l.ankleIndex);
          lo->FieldRef("hitSomething").raw = leBytes(l.hitSomething ? 1u : 0u, 1);
          lo->FieldRef("isPlantedMS").raw = leBytes(l.isPlantedMS ? 1u : 0u, 1);
          lo->FieldRef("isOriginalAnkleTransformMSSet").raw = leBytes(l.isOriginalAnkleTransformMSSet ? 1u : 0u, 1);
          objs.push_back(lo);
      } }
    o->FieldRef("raycastDistanceUp").raw = ff(def.raycastDistanceUp);
    o->FieldRef("raycastDistanceDown").raw = ff(def.raycastDistanceDown);
    o->FieldRef("originalGroundHeightMS").raw = ff(def.originalGroundHeightMS);
    o->FieldRef("errorOut").raw = ff(def.errorOut);
    o->FieldRef("errorOutTranslation").raw = vec4Bytes(def.errorOutTranslation);
    o->FieldRef("alignWithGroundRotation").raw = vec4Bytes(def.alignWithGroundRotation);
    o->FieldRef("verticalOffset").raw = ff(def.verticalOffset);
    o->FieldRef("collisionFilterInfo").raw = leBytes(static_cast<std::uint64_t>(def.collisionFilterInfo), 4);
    o->FieldRef("forwardAlignFraction").raw = ff(def.forwardAlignFraction);
    o->FieldRef("sidewaysAlignFraction").raw = ff(def.sidewaysAlignFraction);
    o->FieldRef("sidewaysSampleWidth").raw = ff(def.sidewaysSampleWidth);
    o->FieldRef("useTrackData").raw = leBytes(def.useTrackData ? 1u : 0u, 1);
    o->FieldRef("lockFeetWhenPlanted").raw = leBytes(def.lockFeetWhenPlanted ? 1u : 0u, 1);
    o->FieldRef("useCharacterUpVector").raw = leBytes(def.useCharacterUpVector ? 1u : 0u, 1);
    o->FieldRef("alignMode").raw = leBytes(static_cast<std::uint64_t>(static_cast<std::uint8_t>(static_cast<std::int8_t>(def.alignMode))), 1);
    return o;
}

// BSIStateManagerModifier — iStateVar + stateData[] (each references a state machine by edge).
std::shared_ptr<io::SchemaObject> BuildIStateManager(const BSIStateManagerModifierDef& def, const schema::SchemaRegistry& reg, const GenResolver& resolve) {
    auto o = make(reg, "BSiStateManagerModifier"); if (!o) return nullptr;
    modBase(*o, def.name, def.userData, reg, def.bindings, &def.enable);
    o->FieldRef("iStateVar").raw = i32(def.iStateVar);
    auto& objs = o->FieldRef("stateData").objs;
    for (const auto& sd : def.stateData) {
        auto so = make(reg, "BSIStateManagerModifierBSiStateData"); if (!so) continue;
        if (!sd.pStateMachine.empty() && sd.pStateMachine != "null") if (auto sm = resolve(sd.pStateMachine)) so->FieldRef("pStateMachine").obj = sm;
        so->FieldRef("StateID").raw       = i32(sd.StateID);
        so->FieldRef("iStateToSetAs").raw = i32(sd.iStateToSetAs);
        objs.push_back(so);
    }
    return o;
}

// ── generic-based specialized modifiers (ragdoll controls, look-at, keyframe) ──
namespace {
std::optional<std::string> gScalar(const GenericModifierDef& def, const char* k) {
    for (const auto& p : def.extraParams) if (p.name == k && p.scalarValue) return p.scalarValue; return std::nullopt;
}
const InlineEventDef* gEvent(const GenericModifierDef& def, const char* k) {
    for (const auto& p : def.extraParams) if (p.name == k && p.eventValue) return &*p.eventValue; return nullptr;
}
const std::vector<GenericInlineObjectEntry>* gObjList(const GenericModifierDef& def, const char* k) {
    for (const auto& p : def.extraParams) if (p.name == k && p.inlineObjectListValue) return &*p.inlineObjectListValue; return nullptr;
}
std::optional<std::string> ef(const GenericInlineObjectEntry& e, const char* k) {
    for (const auto& kv : e.fields) if (kv.first == k) return kv.second; return std::nullopt;
}
void setF_(io::SchemaObject& o, const char* field, const GenericModifierDef& d, const char* key) { if (auto v = gScalar(d,key)) o.FieldRef(field).raw = f32(*v); }
void setB_(io::SchemaObject& o, const char* field, const GenericModifierDef& d, const char* key) { if (auto v = gScalar(d,key)) o.FieldRef(field).raw = leBytes(parseBool(*v)?1u:0u,1); }
void setEv_(io::SchemaObject& o, const char* field, const GenericModifierDef& d, const char* key, const schema::SchemaRegistry& reg) {
    if (auto* e = gEvent(d,key)) if (auto so = std::dynamic_pointer_cast<io::SchemaObject>(o.FieldRef(field).obj)) fillEvent(*so, e->id, e->payload.value_or("null"), reg);
}
} // namespace

// BSRagdollContactListenerModifier — contactEvent + owned bones array (built by caller).
std::shared_ptr<io::SchemaObject> BuildRagdollContactListener(const GenericModifierDef& def, const std::shared_ptr<io::SchemaObject>& bones, const schema::SchemaRegistry& reg) {
    auto o = make(reg, "BSRagdollContactListenerModifier"); if (!o) return nullptr;
    modBase(*o, def.name, def.userData, reg, def.bindings, &def.enable);
    setEv_(*o, "contactEvent", def, "contactEvent", reg);
    if (bones) o->FieldRef("bones").obj = bones;
    return o;
}

// hkbPoweredRagdollControlsModifier — controlData (flat) + worldFromModelModeData + owned bones + boneWeights.
std::shared_ptr<io::SchemaObject> BuildPoweredRagdoll(const GenericModifierDef& def, const std::shared_ptr<io::SchemaObject>& bones,
                                                      const std::shared_ptr<io::SchemaObject>& boneWeights, const schema::SchemaRegistry& reg) {
    auto o = make(reg, "hkbPoweredRagdollControlsModifier"); if (!o) return nullptr;
    modBase(*o, def.name, def.userData, reg, def.bindings, &def.enable);
    if (auto cd = std::dynamic_pointer_cast<io::SchemaObject>(o->FieldRef("controlData").obj)) {
        setF_(*cd,"maxForce",def,"maxForce"); setF_(*cd,"tau",def,"tau"); setF_(*cd,"damping",def,"damping");
        setF_(*cd,"proportionalRecoveryVelocity",def,"proportionalRecoveryVelocity"); setF_(*cd,"constantRecoveryVelocity",def,"constantRecoveryVelocity");
    }
    if (auto wfm = std::dynamic_pointer_cast<io::SchemaObject>(o->FieldRef("worldFromModelModeData").obj)) {
        if (auto v=gScalar(def,"poseMatchingBone0")) wfm->FieldRef("poseMatchingBone0").raw = i16(std::atoi(v->c_str()));
        if (auto v=gScalar(def,"poseMatchingBone1")) wfm->FieldRef("poseMatchingBone1").raw = i16(std::atoi(v->c_str()));
        if (auto v=gScalar(def,"poseMatchingBone2")) wfm->FieldRef("poseMatchingBone2").raw = i16(std::atoi(v->c_str()));
        if (auto v=gScalar(def,"worldFromModelMode")) wfm->FieldRef("mode").raw = leBytes(static_cast<std::uint64_t>(static_cast<std::uint8_t>(std::atoi(v->c_str()))),1);
    }
    if (bones) o->FieldRef("bones").obj = bones;
    if (boneWeights) o->FieldRef("boneWeights").obj = boneWeights;
    return o;
}

// hkbRigidBodyRagdollControlsModifier — controlData (durationToBlend + nested keyFrameHierarchy) + owned bones.
std::shared_ptr<io::SchemaObject> BuildRigidBodyRagdoll(const GenericModifierDef& def, const std::shared_ptr<io::SchemaObject>& bones, const schema::SchemaRegistry& reg) {
    auto o = make(reg, "hkbRigidBodyRagdollControlsModifier"); if (!o) return nullptr;
    modBase(*o, def.name, def.userData, reg, def.bindings, &def.enable);
    if (auto cd = std::dynamic_pointer_cast<io::SchemaObject>(o->FieldRef("controlData").obj)) {
        setF_(*cd,"durationToBlend",def,"durationToBlend");
        if (auto kfh = std::dynamic_pointer_cast<io::SchemaObject>(cd->FieldRef("keyFrameHierarchyControlData").obj)) {
            for (const char* k : {"hierarchyGain","velocityDamping","accelerationGain","velocityGain","positionGain",
                                  "positionMaxLinearVelocity","positionMaxAngularVelocity","snapGain","snapMaxLinearVelocity",
                                  "snapMaxAngularVelocity","snapMaxLinearDistance","snapMaxAngularDistance"})
                setF_(*kfh, k, def, k);
        }
    }
    if (bones) o->FieldRef("bones").obj = bones;
    return o;
}

// hkbKeyframeBonesModifier — keyframeInfo[] inline-object array + owned keyframedBonesList.
std::shared_ptr<io::SchemaObject> BuildKeyframeBones(const GenericModifierDef& def, const std::shared_ptr<io::SchemaObject>& bonesList, const schema::SchemaRegistry& reg) {
    auto o = make(reg, "hkbKeyframeBonesModifier"); if (!o) return nullptr;
    modBase(*o, def.name, def.userData, reg, def.bindings, &def.enable);
    if (auto* list = gObjList(def, "keyframeInfo")) { auto& objs = o->FieldRef("keyframeInfo").objs;
        for (const auto& e : *list) {
            auto k = make(reg, "hkbKeyframeBonesModifierKeyframeInfo"); if (!k) continue;
            if (auto v=ef(e,"keyframedPosition")) k->FieldRef("keyframedPosition").raw = vec4Bytes(*v);
            if (auto v=ef(e,"keyframedRotation")) k->FieldRef("keyframedRotation").raw = vec4Bytes(*v);
            if (auto v=ef(e,"boneIndex")) k->FieldRef("boneIndex").raw = i16(std::atoi(v->c_str()));
            if (auto v=ef(e,"isValid")) k->FieldRef("isValid").raw = leBytes(parseBool(*v)?1u:0u,1);
            objs.push_back(k);
        } }
    if (bonesList) o->FieldRef("keyframedBonesList").obj = bonesList;
    return o;
}

// BSLookAtModifier — many scalars + bones[]/eyeBones[] inline-object arrays + inline event.
std::shared_ptr<io::SchemaObject> BuildLookAt(const GenericModifierDef& def, const schema::SchemaRegistry& reg) {
    auto o = make(reg, "BSLookAtModifier"); if (!o) return nullptr;
    modBase(*o, def.name, def.userData, reg, def.bindings, &def.enable);
    setB_(*o,"lookAtTarget",def,"lookAtTarget");
    auto readBones = [&](const char* key, const char* field) {
        if (auto* list = gObjList(def, key)) { auto& objs = o->FieldRef(field).objs;
            for (const auto& e : *list) {
                auto b = make(reg, "BSLookAtModifierBoneData"); if (!b) continue;
                if (auto v=ef(e,"index")) b->FieldRef("index").raw = i16(std::atoi(v->c_str()));
                if (auto v=ef(e,"fwdAxisLS")) b->FieldRef("fwdAxisLS").raw = vec4Bytes(*v);
                if (auto v=ef(e,"limitAngleDegrees")) b->FieldRef("limitAngleDegrees").raw = f32(*v);
                if (auto v=ef(e,"onGain")) b->FieldRef("onGain").raw = f32(*v);
                if (auto v=ef(e,"offGain")) b->FieldRef("offGain").raw = f32(*v);
                if (auto v=ef(e,"enabled")) b->FieldRef("enabled").raw = leBytes(parseBool(*v)?1u:0u,1);
                objs.push_back(b);
            } }
    };
    readBones("bones","bones"); readBones("eyeBones","eyeBones");
    setF_(*o,"limitAngleDegrees",def,"limitAngleDegrees"); setF_(*o,"limitAngleThresholdDegrees",def,"limitAngleThresholdDegrees");
    setB_(*o,"continueLookOutsideOfLimit",def,"continueLookOutsideOfLimit");
    setF_(*o,"onGain",def,"onGain"); setF_(*o,"offGain",def,"offGain"); setB_(*o,"useBoneGains",def,"useBoneGains");
    if (auto v=gScalar(def,"targetLocation")) o->FieldRef("targetLocation").raw = vec4Bytes(*v);
    setB_(*o,"targetOutsideLimits",def,"targetOutsideLimits");
    setEv_(*o,"targetOutOfLimitEvent",def,"targetOutOfLimitEvent",reg);
    setB_(*o,"lookAtCamera",def,"lookAtCamera");
    setF_(*o,"lookAtCameraX",def,"lookAtCameraX"); setF_(*o,"lookAtCameraY",def,"lookAtCameraY"); setF_(*o,"lookAtCameraZ",def,"lookAtCameraZ");
    return o;
}

// ── graph-level assembler (hkbBehaviorGraph + Data + StringData + VariableValueSet + roster) ──
// hkbBehaviorGraphData — the graph's roster: string data + variable/charprop/event infos + initial
// values. Mirrors BehaviorBuilder::buildGraphData. stringData + variableInitialValues are OWNED here.
std::shared_ptr<io::SchemaObject> BuildGraphData(const BehaviorGraphDataDef& gd, const schema::SchemaRegistry& reg) {
    auto data = make(reg, "hkbBehaviorGraphData"); if (!data) return nullptr;

    // stringData (owned)
    auto sd = make(reg, "hkbBehaviorGraphStringData");
    if (sd) {
        for (const auto& e : gd.events)    sd->FieldRef("eventNames").strs.push_back(e.name);
        for (const auto& v : gd.variables) sd->FieldRef("variableNames").strs.push_back(v.name);
        for (const auto& c : gd.characterPropertyNames) sd->FieldRef("characterPropertyNames").strs.push_back(c.name);
    }

    // variableInfos
    { auto& objs = data->FieldRef("variableInfos").objs;
      for (const auto& v : gd.variables) {
          auto vi = make(reg, "hkbVariableInfo"); if (!vi) continue;
          if (auto r = std::dynamic_pointer_cast<io::SchemaObject>(vi->FieldRef("role").obj)) {
              r->FieldRef("role").raw  = i16(static_cast<int>(enums::ResolveEnum(v.role, enums::Role())));
              r->FieldRef("flags").raw = i16(v.roleFlags);
          }
          vi->FieldRef("type").raw = leBytes(static_cast<std::uint64_t>(enums::ResolveEnum(v.type, enums::VariableType())), 1);
          objs.push_back(vi);
      } }

    // characterPropertyInfos
    { const int n = !gd.characterPropertyNames.empty() ? (int)gd.characterPropertyNames.size() : gd.characterPropertyInfoCount;
      auto& objs = data->FieldRef("characterPropertyInfos").objs;
      for (int i = 0; i < n; ++i) {
          auto vi = make(reg, "hkbVariableInfo"); if (!vi) continue;
          long flags = 0, type = enums::ResolveEnum("VARIABLE_TYPE_POINTER", enums::VariableType());
          if (i < (int)gd.characterPropertyNames.size()) {
              flags = enums::ResolveEnum(gd.characterPropertyNames[(std::size_t)i].flags, enums::RoleFlags());
              type  = enums::ResolveEnum(gd.characterPropertyNames[(std::size_t)i].type, enums::VariableType());
          }
          if (auto r = std::dynamic_pointer_cast<io::SchemaObject>(vi->FieldRef("role").obj)) { r->FieldRef("role").raw = i16(0); r->FieldRef("flags").raw = i16((int)flags); }
          vi->FieldRef("type").raw = leBytes(static_cast<std::uint64_t>(type), 1);
          objs.push_back(vi);
      } }

    // eventInfos
    { auto& objs = data->FieldRef("eventInfos").objs;
      for (const auto& e : gd.events) {
          auto ei = make(reg, "hkbEventInfo"); if (!ei) continue;
          ei->FieldRef("flags").raw = leBytes(static_cast<std::uint64_t>(enums::ResolveEnum(e.flags, enums::EventInfoFlags())), 4);
          objs.push_back(ei);
      } }

    // variableInitialValues (owned hkbVariableValueSet)
    auto vvs = make(reg, "hkbVariableValueSet");
    if (vvs) {
        auto& words = vvs->FieldRef("wordVariableValues").objs;
        for (const auto& v : gd.variables) { auto wv = make(reg, "hkbVariableValue"); if (!wv) continue; wv->FieldRef("value").raw = i32(v.value); words.push_back(wv); }
        std::vector<std::uint8_t>& quad = vvs->FieldRef("quadVariableValues").raw;
        if (!gd.quadVariableValues.empty()) {
            for (const auto& q : gd.quadVariableValues) { auto b = vec4Bytes(q); quad.insert(quad.end(), b.begin(), b.end()); }
        } else {
            // derive per-variable for vector/quaternion types (mirrors BehaviorBuilder::buildGraphData)
            for (const auto& v : gd.variables) {
                if (v.type=="VARIABLE_TYPE_VECTOR4" || v.type=="VARIABLE_TYPE_QUATERNION" || v.type=="VARIABLE_TYPE_VECTOR3") {
                    std::vector<std::uint8_t> b;
                    if (v.quadValue)                          b = vec4Bytes(*v.quadValue);
                    else if (v.type=="VARIABLE_TYPE_QUATERNION") b = vec4Bytes("(0 0 0 1)");
                    else                                      b = vec4Bytes("(0 0 0 0)");
                    quad.insert(quad.end(), b.begin(), b.end());
                }
            }
        }
    }

    if (vvs) data->FieldRef("variableInitialValues").obj = vvs;
    if (sd)  data->FieldRef("stringData").obj = sd;
    return data;
}

// hkbBehaviorGraph — variableMode + rootGenerator edge + data edge (both resolved from the graph).
std::shared_ptr<io::SchemaObject> BuildBehaviorGraph(const BehaviorDef& beh, const schema::SchemaRegistry& reg, const GenResolver& resolve) {
    auto o = make(reg, "hkbBehaviorGraph"); if (!o) return nullptr;
    o->FieldRef("userData").raw = leBytes(0ull, 8);
    o->FieldRef("name").str = beh.name;
    o->FieldRef("variableMode").raw = leBytes(static_cast<std::uint64_t>(enums::ResolveEnum(beh.variableMode, enums::VariableMode())), 1);
    if (!beh.rootGenerator.empty() && beh.rootGenerator != "null") if (auto g = resolve(beh.rootGenerator)) o->FieldRef("rootGenerator").obj = g;
    if (beh.data && *beh.data != "null" && !beh.data->empty()) if (auto d = resolve(*beh.data)) o->FieldRef("data").obj = d;
    return o;
}

// ── THE COMPILER: BehaviorData → whole SchemaObject graph (the runtime compile path) ──
// Memoized build-all: a node's name resolves to its (once-)built SchemaObject; edges resolve through the
// same memo (shared nodes built once — states shared across SMs, generators shared across parents).
// Mirrors the typed BehaviorBuilder::buildNode dispatch, but the Defs are already present in `data`
// (no reverse-extraction). Owned data arrays (expressions/eventRanges/bones) resolve by the id-convention.
std::shared_ptr<io::SchemaObject> AssembleGraph(const BehaviorData& data, const schema::SchemaRegistry& reg) {
    auto memo = std::make_shared<std::unordered_map<std::string, std::shared_ptr<io::SchemaObject>>>();
    GenResolver byName;
    auto boneArr = [&](const std::string& id) -> std::shared_ptr<io::SchemaObject> {
        auto it = data.boneIndexArrays.find(id); return it==data.boneIndexArrays.end() ? nullptr : BuildBoneIndexArray(it->second, reg, data.boneNames); };
    byName = [&, memo](const std::string& name) -> std::shared_ptr<io::SchemaObject> {
        if (name.empty() || name == "null") return nullptr;
        if (auto m = memo->find(name); m != memo->end()) return m->second;
        std::shared_ptr<io::SchemaObject> r;
        if      (auto it=data.clips.find(name);                    it!=data.clips.end())                    r=BuildClip(it->second,reg);
        else if (auto it=data.blenders.find(name);                 it!=data.blenders.end())                 r=BuildBlender(it->second,reg,byName,data.boneNames);
        else if (auto it=data.selectors.find(name);                it!=data.selectors.end())                r=BuildSelector(it->second,reg,byName);
        else if (auto it=data.stateMachines.find(name);            it!=data.stateMachines.end())            r=BuildStateMachine(it->second,reg,byName);
        else if (auto it=data.states.find(name);                   it!=data.states.end())                   r=BuildState(it->second,reg,byName);
        else if (auto it=data.transitionEffects.find(name);        it!=data.transitionEffects.end())        r=BuildTransitionEffect(it->second,reg);
        else if (auto it=data.stateTaggingGenerators.find(name);   it!=data.stateTaggingGenerators.end())   r=BuildStateTagging(it->second,reg,byName);
        else if (auto it=data.behaviorReferences.find(name);       it!=data.behaviorReferences.end())       r=BuildBehaviorReference(it->second,reg);
        else if (auto it=data.gamebryoSequences.find(name);        it!=data.gamebryoSequences.end())        r=BuildGamebryoSequence(it->second,reg);
        else if (auto it=data.cyclicBlendGenerators.find(name);    it!=data.cyclicBlendGenerators.end())    r=BuildCyclicBlend(it->second,reg,byName);
        else if (auto it=data.boneSwitchGenerators.find(name);     it!=data.boneSwitchGenerators.end())     r=BuildBoneSwitch(it->second,reg,byName,data.boneNames);
        else if (auto it=data.modifierGenerators.find(name);       it!=data.modifierGenerators.end())       r=BuildModifierGenerator(it->second,reg,byName);
        else if (auto it=data.offsetAnimGenerators.find(name);     it!=data.offsetAnimGenerators.end())     r=BuildOffsetAnim(it->second,reg,byName);
        else if (auto it=data.modifierLists.find(name);            it!=data.modifierLists.end())            r=BuildModifierList(it->second,reg,byName);
        else if (auto it=data.isActiveModifiers.find(name);        it!=data.isActiveModifiers.end())        r=BuildIsActiveModifier(it->second,reg);
        else if (auto it=data.synchronizedClips.find(name);        it!=data.synchronizedClips.end())        r=BuildSynchronizedClip(it->second,reg,byName);
        else if (auto it=data.poseMatchingGenerators.find(name);   it!=data.poseMatchingGenerators.end())   r=BuildPoseMatching(it->second,reg,byName,data.boneNames);
        else if (auto it=data.referencePoseGenerators.find(name);  it!=data.referencePoseGenerators.end())  r=BuildReferencePose(it->second,reg);
        else if (auto it=data.eventDrivenModifiers.find(name);     it!=data.eventDrivenModifiers.end())     r=BuildEventDrivenModifier(it->second,reg,byName);
        else if (auto it=data.eventEveryNModifiers.find(name);     it!=data.eventEveryNModifiers.end())     r=BuildEventEveryN(it->second,reg);
        else if (auto it=data.interpValueModifiers.find(name);     it!=data.interpValueModifiers.end())     r=BuildInterpValue(it->second,reg);
        else if (auto it=data.footIkControlsModifiers.find(name);  it!=data.footIkControlsModifiers.end())  r=BuildFootIkControls(it->second,reg);
        else if (auto it=data.footIkModifiers.find(name);          it!=data.footIkModifiers.end())          r=BuildFootIkModifier(it->second,reg);
        else if (auto it=data.iStateManagerModifiers.find(name);   it!=data.iStateManagerModifiers.end())   r=BuildIStateManager(it->second,reg,byName);
        else if (auto it=data.evaluateExpressionModifiers.find(name); it!=data.evaluateExpressionModifiers.end()) {
            const auto& d=it->second; const std::string an=(!d.expressions.empty()&&d.expressions!="null")?d.expressions:name+"_expressions";
            std::shared_ptr<io::SchemaObject> arr; if (auto a=data.expressionDataArrays.find(an); a!=data.expressionDataArrays.end()) arr=BuildExpressionDataArray(a->second,reg);
            r=BuildEvaluateExpression(d,reg,arr);
        }
        else if (auto it=data.eventsFromRangeModifiers.find(name); it!=data.eventsFromRangeModifiers.end()) {
            const auto& d=it->second; const std::string an=(d.eventRanges&&*d.eventRanges!="null"&&!d.eventRanges->empty())?*d.eventRanges:name+"_eventRanges";
            std::shared_ptr<io::SchemaObject> arr; if (auto a=data.eventRangeDataArrays.find(an); a!=data.eventRangeDataArrays.end()) arr=BuildEventRangeDataArray(a->second,reg);
            r=BuildEventsFromRange(d,reg,arr);
        }
        else if (auto it=data.genericModifiers.find(name); it!=data.genericModifiers.end()) {
            const auto& d=it->second;
            if      (d.className=="hkbPoweredRagdollControlsModifier")   r=BuildPoweredRagdoll(d, boneArr(name+"_bones"), nullptr, reg);
            else if (d.className=="hkbRigidBodyRagdollControlsModifier") r=BuildRigidBodyRagdoll(d, boneArr(name+"_bones"), reg);
            else if (d.className=="BSRagdollContactListenerModifier")    r=BuildRagdollContactListener(d, boneArr(name+"_bones"), reg);
            else if (d.className=="hkbKeyframeBonesModifier")            r=BuildKeyframeBones(d, boneArr(name+"_keyframedBonesList"), reg);
            else if (d.className=="BSLookAtModifier")                    r=BuildLookAt(d, reg);
            else                                                        r=BuildGenericModifier(d, reg, byName, data.boneNames);
        }
        if (r) (*memo)[name]=r;
        return r;
    };

    auto rootGen   = byName(data.behavior.behavior.rootGenerator);
    auto graphData = data.graphData ? BuildGraphData(*data.graphData, reg) : nullptr;

    auto graph = make(reg, "hkbBehaviorGraph"); if (!graph) return nullptr;
    graph->FieldRef("userData").raw = leBytes(0ull, 8);
    graph->FieldRef("name").str = data.behavior.behavior.name;
    graph->FieldRef("variableMode").raw = leBytes(static_cast<std::uint64_t>(enums::ResolveEnum(data.behavior.behavior.variableMode, enums::VariableMode())), 1);
    if (rootGen)   graph->FieldRef("rootGenerator").obj = rootGen;
    if (graphData) graph->FieldRef("data").obj = graphData;

    auto root = make(reg, "hkRootLevelContainer"); if (!root) return nullptr;
    auto nv = make(reg, "hkRootLevelContainerNamedVariant"); if (!nv) return nullptr;
    nv->FieldRef("name").str = "hkbBehaviorGraph"; nv->FieldRef("className").str = "hkbBehaviorGraph";
    nv->FieldRef("variant").obj = graph;
    root->FieldRef("namedVariants").objs.push_back(nv);

    // Dedup shared value-leaves so identical ones share ONE object — matches the typed builder's
    // _payloadMemo (hkbStringEventPayload by data) + _conditionMemo (hkbExpressionCondition by expression;
    // hkbStringCondition deliberately NOT deduped, per the typed builder). This is why vanilla/typed have
    // fewer objects than a naive per-node build; without it the graph is valid but not byte-identical.
    auto keyOf = [](io::SchemaObject& o) -> std::string {
        const std::string c = o.ClassName();
        if (c == "hkbStringEventPayload")  return "P:" + o.FieldRef("data").str;
        if (c == "hkbExpressionCondition") return "C:" + o.FieldRef("expression").str;
        return {};
    };
    std::unordered_map<std::string, std::shared_ptr<io::SchemaObject>> canon;
    { std::unordered_set<const void*> seen;
      std::function<void(const std::shared_ptr<io::SchemaObject>&)> collect = [&](const std::shared_ptr<io::SchemaObject>& n){
          if (!n || !seen.insert(n.get()).second) return;
          std::string k = keyOf(*n); if (!k.empty()) canon.emplace(k, n);   // first occurrence wins
          for (const auto& fv : n->Values()) { if (fv.obj) collect(std::dynamic_pointer_cast<io::SchemaObject>(fv.obj)); for (const auto& o : fv.objs) collect(std::dynamic_pointer_cast<io::SchemaObject>(o)); }
      };
      collect(root); }
    { std::unordered_set<const void*> seen;
      auto swap = [&](std::shared_ptr<io::SchemaObject>& p){ if (!p) return; std::string k = keyOf(*p); if (!k.empty()) { auto it = canon.find(k); if (it != canon.end()) p = it->second; } };
      std::function<void(io::SchemaObject&)> rw = [&](io::SchemaObject& n){
          if (!seen.insert(&n).second) return;
          const std::size_t nf = n.Fields().size();
          for (std::size_t i=0;i<nf;++i) { auto& fv = n.FieldAt(i);
              if (fv.obj) { auto so = std::dynamic_pointer_cast<io::SchemaObject>(fv.obj); if (so) { swap(so); fv.obj = so; rw(*so); } }
              for (auto& o : fv.objs) { auto so = std::dynamic_pointer_cast<io::SchemaObject>(o); if (so) { swap(so); o = so; rw(*so); } }
          }
      };
      rw(*root); }
    return root;
}

// hkbProjectData (+ owned hkbProjectStringData) wrapped in hkRootLevelContainer — the schema-driven
// equivalent of havok-core's typed BuildProject. A project is a FLAT record (no generator/state
// recursion), so this is a direct field map. Byte-gated == the typed path; havok-core's BuildProject
// dispatches here when the schema compiler is on.
std::shared_ptr<io::SchemaObject> AssembleProject(const ProjectSpec& spec, const schema::SchemaRegistry& reg) {
    auto sd = make(reg, "hkbProjectStringData");
    if (!sd) return nullptr;
    for (const auto& s : spec.animationFilenames) sd->FieldRef("animationFilenames").strs.push_back(s);
    for (const auto& s : spec.behaviorFilenames)  sd->FieldRef("behaviorFilenames").strs.push_back(s);
    for (const auto& s : spec.characterFilenames) sd->FieldRef("characterFilenames").strs.push_back(s);
    for (const auto& s : spec.eventNames)         sd->FieldRef("eventNames").strs.push_back(s);
    sd->FieldRef("animationPath").str    = spec.animationPath;
    sd->FieldRef("behaviorPath").str     = spec.behaviorPath;
    sd->FieldRef("characterPath").str    = spec.characterPath;
    sd->FieldRef("fullPathToSource").str = spec.fullPathToSource;

    auto data = make(reg, "hkbProjectData");
    if (!data) return nullptr;
    { std::vector<std::uint8_t> up; up.reserve(16);   // worldUpWS: 4 LE floats
      for (float c : spec.worldUpWS) { std::uint8_t b[4]; std::memcpy(b, &c, 4); up.insert(up.end(), b, b + 4); }
      data->FieldRef("worldUpWS").raw = std::move(up); }
    data->FieldRef("stringData").obj       = sd;
    data->FieldRef("defaultEventMode").raw = { static_cast<std::uint8_t>(spec.defaultEventMode) };

    auto root = make(reg, "hkRootLevelContainer"); if (!root) return nullptr;
    auto nv   = make(reg, "hkRootLevelContainerNamedVariant"); if (!nv) return nullptr;
    nv->FieldRef("name").str      = "hkbProjectData";
    nv->FieldRef("className").str = "hkbProjectData";
    nv->FieldRef("variant").obj   = data;
    root->FieldRef("namedVariants").objs.push_back(nv);
    return root;
}

// hkbCharacterData (+ owned hkbVariableValueSet / hkbFootIkDriverInfo / hkbCharacterStringData /
// hkbMirroredSkeletonInfo / a per-pointer-property hkbBoneWeightArray), wrapped in hkRootLevelContainer
// — the schema-driven equivalent of havok-core's typed CharacterBuilder::Build. Mirrors it field for
// field; byte-gated == typed. havok-core's CompileCharacter dispatches here when the schema compiler
// is on. A character is a fixed-shape record (no generator/state recursion).
std::shared_ptr<io::SchemaObject> AssembleCharacter(const CharacterData& d, const schema::SchemaRegistry& reg) {
    const CharacterDef& ch = d.character;
    auto f32b  = [](float v) { std::uint8_t b[4]; std::memcpy(b, &v, 4); return std::vector<std::uint8_t>(b, b + 4); };
    auto vec4b = [](const std::array<float, 4>& a) { std::vector<std::uint8_t> r; r.reserve(16);
        for (float c : a) { std::uint8_t b[4]; std::memcpy(b, &c, 4); r.insert(r.end(), b, b + 4); } return r; };
    auto u32b  = [](std::uint32_t v) { return leBytes(static_cast<std::uint64_t>(v), 4); };
    auto b1b   = [](bool v) { return std::vector<std::uint8_t>{ static_cast<std::uint8_t>(v ? 1 : 0) }; };
    auto toF   = [](const std::string& s) { try { return std::stof(s); } catch (...) { return 0.f; } };
    auto findBone = [&](const std::string& n) -> int {
        for (int i = 0; i < static_cast<int>(d.boneNames.size()); ++i) if (d.boneNames[static_cast<std::size_t>(i)] == n) return i;
        return -1; };

    // 1) hkbBoneWeightArray — one per POINTER property, in property order (raw path == vanilla; named
    //    path resolves against the skeleton bone order, default 0.0). Mirrors CharacterBuilder::buildBoneWeights.
    std::vector<std::shared_ptr<io::SchemaObject>> boneWeights;
    for (const auto& p : d.properties) {
        if (!p.isPointer()) continue;
        auto arr = make(reg, "hkbBoneWeightArray"); if (!arr) return nullptr;
        if (p.boneWeights) {
            const auto& bw = *p.boneWeights;
            std::vector<std::uint8_t>& raw = arr->FieldRef("boneWeights").raw;
            if (!bw.named.empty() || bw.boneCount) {
                const int out = bw.boneCount.value_or(static_cast<int>(d.boneNames.size()));
                std::vector<float> w(static_cast<std::size_t>(out < 0 ? 0 : out), 0.f);
                for (const auto& [bone, weight] : bw.named) { int idx = findBone(bone); if (idx >= 0 && idx < out) w[static_cast<std::size_t>(idx)] = toF(weight); }
                for (float f : w) { std::uint8_t b[4]; std::memcpy(b, &f, 4); raw.insert(raw.end(), b, b + 4); }
            } else if (bw.count > 0 && !bw.values.empty()) {
                std::stringstream ss(bw.values); float f; while (ss >> f) { std::uint8_t b[4]; std::memcpy(b, &f, 4); raw.insert(raw.end(), b, b + 4); }
            }
        }
        boneWeights.push_back(arr);
    }

    // 2) hkbVariableValueSet — word value per property (pointer -> index, else initial); bone-weight
    //    arrays as the variant values.
    auto vvs = make(reg, "hkbVariableValueSet"); if (!vvs) return nullptr;
    { auto& words = vvs->FieldRef("wordVariableValues").objs;
      int pointerIndex = 0;
      for (const auto& p : d.properties) {
          auto wv = make(reg, "hkbVariableValue"); if (!wv) return nullptr;
          const int value = p.isPointer() ? pointerIndex++ : static_cast<int>(p.initialValue.value_or(0));
          wv->FieldRef("value").raw = i32(value);
          words.push_back(wv);
      }
      auto& variants = vvs->FieldRef("variantVariableValues").objs;
      for (const auto& b : boneWeights) variants.push_back(b);
    }

    // 3) hkbFootIkDriverInfo
    auto ik = make(reg, "hkbFootIkDriverInfo"); if (!ik) return nullptr;
    { auto& legs = ik->FieldRef("legs").objs;
      for (const auto& L : d.footIk.legs) {
          auto leg = make(reg, "hkbFootIkDriverInfoLeg"); if (!leg) return nullptr;
          leg->FieldRef("kneeAxisLS").raw = vec4b(L.kneeAxisLS);
          leg->FieldRef("footEndLS").raw  = vec4b(L.footEndLS);
          leg->FieldRef("footPlantedAnkleHeightMS").raw = f32b(L.footPlantedAnkleHeightMS);
          leg->FieldRef("footRaisedAnkleHeightMS").raw  = f32b(L.footRaisedAnkleHeightMS);
          leg->FieldRef("maxAnkleHeightMS").raw  = f32b(L.maxAnkleHeightMS);
          leg->FieldRef("minAnkleHeightMS").raw  = f32b(L.minAnkleHeightMS);
          leg->FieldRef("maxKneeAngleDegrees").raw  = f32b(L.maxKneeAngleDegrees);
          leg->FieldRef("minKneeAngleDegrees").raw  = f32b(L.minKneeAngleDegrees);
          leg->FieldRef("maxAnkleAngleDegrees").raw = f32b(L.maxAnkleAngleDegrees);
          leg->FieldRef("hipIndex").raw   = i16(L.hipIndex);
          leg->FieldRef("kneeIndex").raw  = i16(L.kneeIndex);
          leg->FieldRef("ankleIndex").raw = i16(L.ankleIndex);
          legs.push_back(leg);
      }
      ik->FieldRef("raycastDistanceUp").raw      = f32b(d.footIk.raycastDistanceUp);
      ik->FieldRef("raycastDistanceDown").raw    = f32b(d.footIk.raycastDistanceDown);
      ik->FieldRef("originalGroundHeightMS").raw = f32b(d.footIk.originalGroundHeightMS);
      ik->FieldRef("verticalOffset").raw         = f32b(d.footIk.verticalOffset);
      ik->FieldRef("collisionFilterInfo").raw    = u32b(static_cast<std::uint32_t>(d.footIk.collisionFilterInfo));
      ik->FieldRef("forwardAlignFraction").raw   = f32b(d.footIk.forwardAlignFraction);
      ik->FieldRef("sidewaysAlignFraction").raw  = f32b(d.footIk.sidewaysAlignFraction);
      ik->FieldRef("sidewaysSampleWidth").raw    = f32b(d.footIk.sidewaysSampleWidth);
      ik->FieldRef("lockFeetWhenPlanted").raw    = b1b(d.footIk.lockFeetWhenPlanted);
      ik->FieldRef("useCharacterUpVector").raw   = b1b(d.footIk.useCharacterUpVector);
      ik->FieldRef("isQuadrupedNarrow").raw      = b1b(d.footIk.isQuadrupedNarrow);
    }

    // 4) hkbCharacterStringData
    auto sd = make(reg, "hkbCharacterStringData"); if (!sd) return nullptr;
    for (const auto& a : d.animations) sd->FieldRef("animationNames").strs.push_back(a);
    for (const auto& p : d.properties) sd->FieldRef("characterPropertyNames").strs.push_back(p.name);
    sd->FieldRef("name").str             = ch.name;
    sd->FieldRef("rigName").str          = ch.rig;
    sd->FieldRef("ragdollName").str      = ch.ragdoll;
    sd->FieldRef("behaviorFilename").str = ch.behavior;

    // 5) hkbMirroredSkeletonInfo
    auto mi = make(reg, "hkbMirroredSkeletonInfo"); if (!mi) return nullptr;
    mi->FieldRef("mirrorAxis").raw = vec4b(d.mirror.mirrorAxis);
    { const BonePairMapDef& bp = d.mirror.bonePairMap;
      std::vector<std::uint8_t>& raw = mi->FieldRef("bonePairMap").raw;
      if (bp.isNamed()) {
          std::vector<std::int16_t> map(d.boneNames.size());
          for (int i = 0; i < static_cast<int>(map.size()); ++i) map[static_cast<std::size_t>(i)] = static_cast<std::int16_t>(i);
          for (const auto& [from, to] : bp.named) { int fi = findBone(from), ti = findBone(to); if (fi >= 0 && ti >= 0) map[static_cast<std::size_t>(fi)] = static_cast<std::int16_t>(ti); }
          for (std::int16_t v : map) { auto b = i16(v); raw.insert(raw.end(), b.begin(), b.end()); }
      } else if (bp.count > 0 && !bp.values.empty()) {
          std::stringstream ss(bp.values); int v; while (ss >> v) { auto b = i16(v); raw.insert(raw.end(), b.begin(), b.end()); }
      }
    }

    // 6) hkbCharacterData
    auto cd = make(reg, "hkbCharacterData"); if (!cd) return nullptr;
    if (auto cci = std::dynamic_pointer_cast<io::SchemaObject>(cd->FieldRef("characterControllerInfo").obj)) {
        cci->FieldRef("capsuleHeight").raw       = f32b(ch.controller.capsuleHeight);
        cci->FieldRef("capsuleRadius").raw       = f32b(ch.controller.capsuleRadius);
        cci->FieldRef("collisionFilterInfo").raw = u32b(static_cast<std::uint32_t>(ch.controller.collisionFilterInfo));
    }
    cd->FieldRef("modelUpMS").raw      = vec4b(ch.model.up);
    cd->FieldRef("modelForwardMS").raw = vec4b(ch.model.forward);
    cd->FieldRef("modelRightMS").raw   = vec4b(ch.model.right);
    { auto& infos = cd->FieldRef("characterPropertyInfos").objs;
      for (const auto& p : d.properties) {
          auto vi = make(reg, "hkbVariableInfo"); if (!vi) return nullptr;
          if (auto r = std::dynamic_pointer_cast<io::SchemaObject>(vi->FieldRef("role").obj)) {
              r->FieldRef("role").raw  = i16(enums::ResolveEnum(p.role, enums::Role()));
              r->FieldRef("flags").raw = i16(0);
          }
          vi->FieldRef("type").raw = leBytes(static_cast<std::uint64_t>(enums::ResolveEnum(p.type, enums::VariableType())), 1);
          infos.push_back(vi);
      }
    }
    cd->FieldRef("characterPropertyValues").obj = vvs;
    cd->FieldRef("footIkDriverInfo").obj        = ik;
    cd->FieldRef("stringData").obj              = sd;
    cd->FieldRef("mirroredSkeletonInfo").obj    = mi;
    cd->FieldRef("scale").raw                   = f32b(ch.scale);

    // 7) hkRootLevelContainer
    auto root = make(reg, "hkRootLevelContainer"); if (!root) return nullptr;
    auto nv   = make(reg, "hkRootLevelContainerNamedVariant"); if (!nv) return nullptr;
    nv->FieldRef("name").str      = "hkbCharacterData";
    nv->FieldRef("className").str = "hkbCharacterData";
    nv->FieldRef("variant").obj   = cd;
    root->FieldRef("namedVariants").objs.push_back(nv);
    return root;
}

// hkRootLevelContainer — one namedVariant wrapping the behavior graph.
std::shared_ptr<io::SchemaObject> BuildRootContainer(const std::string& graphKey, const schema::SchemaRegistry& reg, const GenResolver& resolve) {
    auto o = make(reg, "hkRootLevelContainer"); if (!o) return nullptr;
    auto nv = make(reg, "hkRootLevelContainerNamedVariant"); if (!nv) return nullptr;
    nv->FieldRef("name").str = "hkbBehaviorGraph";
    nv->FieldRef("className").str = "hkbBehaviorGraph";
    if (!graphKey.empty()) if (auto g = resolve(graphKey)) nv->FieldRef("variant").obj = g;
    o->FieldRef("namedVariants").objs.push_back(nv);
    return o;
}

// ── generic modifier (schema-driven field-walk; covers the flat Bethesda modifiers) ──
namespace {
// enum-table-by-name (the subset the generic modifiers reference), mirroring HavokModel.cpp's emit map.
const std::unordered_map<std::string,long>* genEnumTable(const std::string& n) {
    using namespace enums;
    static const std::unordered_map<std::string, const std::unordered_map<std::string,long>*> reg = {
        {"SetAngleMethod", &SetAngleMethod()}, {"RotationAxisCoordinates", &RotationAxisCoordinates()},
        {"BlendCurve", &BlendCurve()}, {"BlendModeFunction", &BlendModeFunction()}, {"EventMode", &EventMode()},
        {"EndMode", &EndMode()}, {"FlagBits", &FlagBits()}, {"TransitionFlags", &TransitionFlags()},
        {"BindingType", &BindingType()}, {"StartStateMode", &StartStateMode()}, {"BlenderFlags", &BlenderFlags()},
    };
    auto it = reg.find(n); return it == reg.end() ? nullptr : it->second;
}
bool isHeaderName(const std::string& n) { return n=="name" || n=="userData" || n=="enable" || n=="variableBindingSet"; }
} // namespace

// Build any FLAT generic modifier (hkbTwist/Timer/Damping/Rotate/GetUp, BSDirectAt/ModifyOnce/
// EventOnDeactivate/EventOnFalseToTrue/SpeedSampler/RagdollContactListener) from its className + params,
// by walking the class SCHEMA and encoding each authored param per the field's kind. NOT for the
// array/owned-sub-node modifiers (LookAt, KeyframeBones, FootIk, ragdoll) — those get dedicated builders.
std::shared_ptr<io::SchemaObject> BuildGenericModifier(const GenericModifierDef& def, const schema::SchemaRegistry& reg, const GenResolver& resolve,
                                                      const std::vector<std::string>& boneNames) {
    auto o = make(reg, def.className.c_str()); if (!o) return nullptr;
    modBase(*o, def.name, def.userData, reg, def.bindings, &def.enable);
    auto findParam = [&](const std::string& nm) -> const GenericParam* {
        for (const auto& p : def.extraParams) if (p.name == nm) return &p; return nullptr;
    };
    using K = schema::FieldKind;
    for (const auto* fp : o->Fields()) {
        const auto& f = *fp;
        if (f.ignored || f.name.empty() || isHeaderName(f.name)) continue;
        const GenericParam* p = findParam(f.name);
        if (!p) continue;
        switch (f.kind) {
            case K::Scalar: {
                if (!p->scalarValue) break;
                const std::string& s = *p->scalarValue;
                if (f.scalar == schema::Scalar::Float) o->FieldRef(f.name).raw = f32(s);
                else if (f.scalar == schema::Scalar::Bool) o->FieldRef(f.name).raw = leBytes(parseBool(s) ? 1u : 0u, 1);
                else {
                    long v;
                    if (!f.enumName.empty()) { auto* t = genEnumTable(f.enumName); v = t ? enums::ResolveEnum(s, *t) : std::strtol(s.c_str(), nullptr, 10); }
                    else {
                        // numeric → direct; otherwise a bone NAME → resolve against the skeleton (typed setBone).
                        char* end = nullptr; v = std::strtol(s.c_str(), &end, 10);
                        if (!(end != s.c_str() && *end == '\0')) { v = -1; for (int i=0;i<(int)boneNames.size();++i) if (boneNames[(std::size_t)i]==s) { v=i; break; } }
                    }
                    o->FieldRef(f.name).raw = leBytes(static_cast<std::uint64_t>(v), schema::ScalarWidth(f.scalar));
                }
                break;
            }
            case K::Vector4: case K::Quaternion:
                if (p->scalarValue) o->FieldRef(f.name).raw = vec4Bytes(*p->scalarValue);
                break;
            case K::Struct:
                if (f.ref == "hkbEventProperty" && p->eventValue)
                    if (auto e = std::dynamic_pointer_cast<io::SchemaObject>(o->FieldRef(f.name).obj)) fillEvent(*e, p->eventValue->id, p->eventValue->payload.value_or("null"), reg);
                break;
            case K::Ptr: {
                // a node reference — the decompiler stores it as a scalar name (typed refMod reads findScalar);
                // accept refValue too for robustness.
                const std::string* rv = (p->refValue && !p->refValue->empty()) ? &*p->refValue
                                      : (p->scalarValue ? &*p->scalarValue : nullptr);
                if (rv && *rv != "null" && !rv->empty()) if (auto n = resolve(*rv)) o->FieldRef(f.name).obj = n;
                break;
            }
            default: break;   // arrays / owned sub-nodes — not on the flat path
        }
    }
    return o;
}

// ── simple generators + bone-index array (batch 3) ────────────────────────────
// hkbBehaviorReferenceGenerator (RBG) — references another behavior by name. Base hkbGenerator.
std::shared_ptr<io::SchemaObject> BuildBehaviorReference(const BehaviorReferenceGeneratorDef& def, const schema::SchemaRegistry& reg) {
    auto o = make(reg, "hkbBehaviorReferenceGenerator"); if (!o) return nullptr;
    modBase(*o, def.name, def.userData, reg, def.bindings);
    o->FieldRef("behaviorName").str = def.behaviorName;
    return o;
}

// BSiStateTaggingGenerator — default generator edge + iState/priority.
std::shared_ptr<io::SchemaObject> BuildStateTagging(const BSiStateTaggingGeneratorDef& def, const schema::SchemaRegistry& reg, const GenResolver& resolve) {
    auto o = make(reg, "BSiStateTaggingGenerator"); if (!o) return nullptr;
    modBase(*o, def.name, def.userData, reg, def.bindings);
    if (!def.pDefaultGenerator.empty() && def.pDefaultGenerator != "null") if (auto g = resolve(def.pDefaultGenerator)) o->FieldRef("pDefaultGenerator").obj = g;
    o->FieldRef("iStateToSetAs").raw = i32(def.iStateToSetAs);
    o->FieldRef("iPriority").raw     = i32(def.iPriority);
    return o;
}

// BSCyclicBlendTransitionGenerator — wraps a blender generator + 2 inline events + blend params.
std::shared_ptr<io::SchemaObject> BuildCyclicBlend(const BSCyclicBlendTransitionGeneratorDef& def, const schema::SchemaRegistry& reg, const GenResolver& resolve) {
    auto o = make(reg, "BSCyclicBlendTransitionGenerator"); if (!o) return nullptr;
    modBase(*o, def.name, def.userData, reg, def.bindings);
    if (!def.pBlenderGenerator.empty() && def.pBlenderGenerator != "null") if (auto g = resolve(def.pBlenderGenerator)) o->FieldRef("pBlenderGenerator").obj = g;
    if (auto e = std::dynamic_pointer_cast<io::SchemaObject>(o->FieldRef("EventToFreezeBlendValue").obj)) fillEvent(*e, def.eventToFreezeBlendValue.id, def.eventToFreezeBlendValue.payload.value_or("null"), reg);
    if (auto e = std::dynamic_pointer_cast<io::SchemaObject>(o->FieldRef("EventToCrossBlend").obj))     fillEvent(*e, def.eventToCrossBlend.id,     def.eventToCrossBlend.payload.value_or("null"), reg);
    o->FieldRef("fBlendParameter").raw     = f32(def.fBlendParameter);
    o->FieldRef("fTransitionDuration").raw = f32(def.fTransitionDuration);
    o->FieldRef("eBlendCurve").raw = leBytes(static_cast<std::uint64_t>(enums::ResolveEnum(def.eBlendCurve, enums::BlendCurve())), 1);
    return o;
}

// hkbReferencePoseGenerator — no authored fields beyond the base (skeleton is ignored/null).
std::shared_ptr<io::SchemaObject> BuildReferencePose(const ReferencePoseGeneratorDef& def, const schema::SchemaRegistry& reg) {
    auto o = make(reg, "hkbReferencePoseGenerator"); if (!o) return nullptr;
    modBase(*o, def.name, def.userData, reg, def.bindings);
    return o;
}

// BGSGamebryoSequenceGenerator — .kf sequence name + blend-mode + percent.
std::shared_ptr<io::SchemaObject> BuildGamebryoSequence(const BGSGamebryoSequenceGeneratorDef& def, const schema::SchemaRegistry& reg) {
    auto o = make(reg, "BGSGamebryoSequenceGenerator"); if (!o) return nullptr;
    modBase(*o, def.name, def.userData, reg, def.bindings);
    o->FieldRef("pSequence").str = def.sequence;
    o->FieldRef("eBlendModeFunction").raw = leBytes(static_cast<std::uint64_t>(enums::ResolveEnum(def.blendModeFunction, enums::BlendModeFunction())), 1);
    o->FieldRef("fPercent").raw = f32(def.percent);
    return o;
}

// hkbBoneIndexArray — int16 scalararray of bone indices. Numeric indices (decompiled-with-skeleton, or
// our own source) are used directly; a NAME-sourced array resolves each name against the skeleton bone
// list (mirrors BehaviorBuilder::buildBoneIndexArray). `boneNames` = the actor skeleton roster.
std::shared_ptr<io::SchemaObject> BuildBoneIndexArray(const BoneIndexArrayDef& def, const schema::SchemaRegistry& reg,
                                                      const std::vector<std::string>& boneNames) {
    auto o = make(reg, "hkbBoneIndexArray"); if (!o) return nullptr;
    std::vector<std::uint8_t>& raw = o->FieldRef("boneIndices").raw;
    if (!def.boneIndices.empty() || def.boneNames.empty()) {
        for (int v : def.boneIndices) { auto b = i16(v); raw.insert(raw.end(), b.begin(), b.end()); }
    } else {
        for (const auto& bn : def.boneNames) {
            int idx = -1;
            for (int i = 0; i < (int)boneNames.size(); ++i) if (boneNames[(std::size_t)i] == bn) { idx = i; break; }
            auto b = i16(idx); raw.insert(raw.end(), b.begin(), b.end());
        }
    }
    return o;
}

// BSBoneSwitchGenerator — default generator edge + ChildrenA[] (each: generator edge + owned bone-weight array).
std::shared_ptr<io::SchemaObject> BuildBoneSwitch(const BSBoneSwitchGeneratorDef& def, const schema::SchemaRegistry& reg, const GenResolver& resolve, const std::vector<std::string>& boneNames) {
    auto o = make(reg, "BSBoneSwitchGenerator"); if (!o) return nullptr;
    modBase(*o, def.name, def.userData, reg, def.bindings);
    if (!def.pDefaultGenerator.empty() && def.pDefaultGenerator != "null") if (auto g = resolve(def.pDefaultGenerator)) o->FieldRef("pDefaultGenerator").obj = g;
    if (def.children) { auto& kids = o->FieldRef("ChildrenA").objs;
        for (const auto& c : *def.children) {
            auto d = make(reg, "BSBoneSwitchGeneratorBoneData"); if (!d) continue;
            if (!c.pGenerator.empty() && c.pGenerator != "null") if (auto g = resolve(c.pGenerator)) d->FieldRef("pGenerator").obj = g;
            if (c.boneWeights) { if (auto bwa = BuildBoneWeights(*c.boneWeights, reg, boneNames)) d->FieldRef("spBoneWeight").obj = bwa; }
            if (c.bindings) { if (auto b = BuildBindingSet(*c.bindings, reg)) d->FieldRef("variableBindingSet").obj = b; }
            kids.push_back(d);
        } }
    return o;
}

// hkbPoseMatchingGenerator — a hkbBlenderGenerator subclass (children + blend params) plus pose-match
// params. flags is authored as a STRING (BlenderFlags table), unlike hkbBlenderGenerator's int flags.
std::shared_ptr<io::SchemaObject> BuildPoseMatching(const PoseMatchingGeneratorDef& def, const schema::SchemaRegistry& reg, const GenResolver& resolve, const std::vector<std::string>& boneNames) {
    static const std::unordered_map<std::string, long> kPoseMatchingMode = { {"MODE_MATCH", 0}, {"MODE_PLAY", 1} };
    auto o = make(reg, "hkbPoseMatchingGenerator"); if (!o) return nullptr;
    // hkbBlenderGenerator base (mirrors BuildBlender).
    o->FieldRef("userData").raw = leBytes(static_cast<std::uint64_t>(def.userData), 8);
    o->FieldRef("name").str     = def.name;
    o->FieldRef("referencePoseWeightThreshold").raw = f32(def.referencePoseWeightThreshold);
    o->FieldRef("blendParameter").raw               = f32(def.blendParameter);
    o->FieldRef("minCyclicBlendParameter").raw      = f32(def.minCyclicBlendParameter);
    o->FieldRef("maxCyclicBlendParameter").raw      = f32(def.maxCyclicBlendParameter);
    o->FieldRef("indexOfSyncMasterChild").raw = i16(def.indexOfSyncMasterChild);
    o->FieldRef("flags").raw                  = leBytes(static_cast<std::uint64_t>(enums::ResolveEnum(def.flags, enums::BlenderFlags())), 2);
    o->FieldRef("subtractLastChild").raw      = b1(def.subtractLastChild);
    if (def.bindings) { if (auto b = BuildBindingSet(*def.bindings, reg)) o->FieldRef("variableBindingSet").obj = b; }
    if (def.children) { auto& kids = o->FieldRef("children").objs;
        for (const auto& c : *def.children) { if (c.generator.empty() || c.generator == "null") continue; if (auto ch = BuildBlenderChild(c, reg, resolve, boneNames)) kids.push_back(ch); } }
    // hkbPoseMatchingGenerator-specific.
    o->FieldRef("worldFromModelRotation").raw = vec4Bytes(def.worldFromModelRotation);
    o->FieldRef("blendSpeed").raw             = f32(def.blendSpeed);
    o->FieldRef("minSpeedToSwitch").raw       = f32(def.minSpeedToSwitch);
    o->FieldRef("minSwitchTimeNoError").raw   = f32(def.minSwitchTimeNoError);
    o->FieldRef("minSwitchTimeFullError").raw = f32(def.minSwitchTimeFullError);
    o->FieldRef("startPlayingEventId").raw  = i32(def.startPlayingEventId);
    o->FieldRef("startMatchingEventId").raw = i32(def.startMatchingEventId);
    o->FieldRef("rootBoneIndex").raw    = i16(def.rootBoneIndex);
    o->FieldRef("otherBoneIndex").raw   = i16(def.otherBoneIndex);
    o->FieldRef("anotherBoneIndex").raw = i16(def.anotherBoneIndex);
    o->FieldRef("pelvisIndex").raw      = i16(def.pelvisIndex);
    o->FieldRef("mode").raw = leBytes(static_cast<std::uint64_t>(enums::ResolveEnum(def.mode, kPoseMatchingMode)), 1);
    return o;
}

// BSSynchronizedClipGenerator — wraps a clip generator with sync params. Base hkbGenerator (no enable).
// The runtime mark transforms (StartMark*/fCurrentLerp/bAtMark/…) are NOT authored — left at Init default.
std::shared_ptr<io::SchemaObject> BuildSynchronizedClip(const BSSynchronizedClipGeneratorDef& def, const schema::SchemaRegistry& reg, const GenResolver& resolve) {
    auto o = make(reg, "BSSynchronizedClipGenerator"); if (!o) return nullptr;
    modBase(*o, def.name, def.userData, reg, def.bindings);
    if (!def.pClipGenerator.empty() && def.pClipGenerator != "null") if (auto g = resolve(def.pClipGenerator)) o->FieldRef("pClipGenerator").obj = g;
    o->FieldRef("SyncAnimPrefix").str = def.syncAnimPrefix;
    o->FieldRef("bSyncClipIgnoreMarkPlacement").raw = b1(def.bSyncClipIgnoreMarkPlacement);
    o->FieldRef("fGetToMarkTime").raw      = f32(def.fGetToMarkTime);
    o->FieldRef("fMarkErrorThreshold").raw = f32(def.fMarkErrorThreshold);
    o->FieldRef("bLeadCharacter").raw      = b1(def.bLeadCharacter);
    o->FieldRef("bReorientSupportChar").raw = b1(def.bReorientSupportChar);
    o->FieldRef("bApplyMotionFromRoot").raw = b1(def.bApplyMotionFromRoot);
    o->FieldRef("sAnimationBindingIndex").raw = leBytes(static_cast<std::uint64_t>(static_cast<std::uint16_t>(static_cast<std::int16_t>(def.sAnimationBindingIndex))), 2);
    return o;
}

// ── data arrays + the modifiers that own them (batch 2) ───────────────────────
// hkbExpressionDataArray{ expressionsData[]: hkbExpressionData }. Post Stage-4, assignment
// variable/event names are pre-resolved to indices — set directly. eventMode uses the shared table
// (the gate feeds a numeric string, which ResolveEnum returns verbatim regardless of table).
std::shared_ptr<io::SchemaObject> BuildExpressionDataArray(const ExpressionDataArrayDef& def, const schema::SchemaRegistry& reg) {
    auto o = make(reg, "hkbExpressionDataArray"); if (!o) return nullptr;
    auto& objs = o->FieldRef("expressionsData").objs;
    for (const auto& e : def.expressionsData) {
        auto d = make(reg, "hkbExpressionData"); if (!d) continue;
        d->FieldRef("expression").str = e.expression;
        d->FieldRef("assignmentVariableIndex").raw = leBytes(static_cast<std::uint64_t>(static_cast<std::uint32_t>(e.assignmentVariableIndex)), 4);
        d->FieldRef("assignmentEventIndex").raw    = leBytes(static_cast<std::uint64_t>(static_cast<std::uint32_t>(e.assignmentEventIndex)), 4);
        d->FieldRef("eventMode").raw = leBytes(static_cast<std::uint64_t>(enums::ResolveEnum(e.eventMode, enums::ExpressionEventMode())), 1);
        objs.push_back(d);
    }
    return o;
}

// hkbEventRangeDataArray{ eventData[]: hkbEventRangeData{ upperBound, inline event, eventMode } }.
std::shared_ptr<io::SchemaObject> BuildEventRangeDataArray(const EventRangeDataArrayDef& def, const schema::SchemaRegistry& reg) {
    static const std::unordered_map<std::string, long> kEventRangeMode = {
        {"EVENT_MODE_SEND_ONCE", 0}, {"EVENT_MODE_SEND_ON_TRUE", 1},
        {"EVENT_MODE_SEND_ON_FALSE_TO_TRUE", 2}, {"EVENT_MODE_SEND_EVERY_FRAME_ONCE_TRUE", 3},
    };
    auto o = make(reg, "hkbEventRangeDataArray"); if (!o) return nullptr;
    auto& objs = o->FieldRef("eventData").objs;
    for (const auto& e : def.eventData) {
        auto d = make(reg, "hkbEventRangeData"); if (!d) continue;
        d->FieldRef("upperBound").raw = f32(e.upperBound);
        if (auto ev = std::dynamic_pointer_cast<io::SchemaObject>(d->FieldRef("event").obj)) fillEvent(*ev, e.eventId, e.payload.value_or("null"), reg);
        d->FieldRef("eventMode").raw = leBytes(static_cast<std::uint64_t>(enums::ResolveEnum(e.eventMode, kEventRangeMode)), 1);
        objs.push_back(d);
    }
    return o;
}

// hkbEvaluateExpressionModifier — owns an hkbExpressionDataArray (built by the caller and passed in).
std::shared_ptr<io::SchemaObject> BuildEvaluateExpression(const EvaluateExpressionModifierDef& def, const schema::SchemaRegistry& reg,
                                                          const std::shared_ptr<io::SchemaObject>& expressions) {
    auto o = make(reg, "hkbEvaluateExpressionModifier"); if (!o) return nullptr;
    modBase(*o, def.name, def.userData, reg, def.bindings, &def.enable);
    if (expressions) o->FieldRef("expressions").obj = expressions;
    return o;
}

// hkbEventsFromRangeModifier — owns an hkbEventRangeDataArray (built by the caller and passed in).
std::shared_ptr<io::SchemaObject> BuildEventsFromRange(const EventsFromRangeModifierDef& def, const schema::SchemaRegistry& reg,
                                                       const std::shared_ptr<io::SchemaObject>& eventRanges) {
    auto o = make(reg, "hkbEventsFromRangeModifier"); if (!o) return nullptr;
    modBase(*o, def.name, def.userData, reg, def.bindings, &def.enable);
    o->FieldRef("inputValue").raw = f32(def.inputValue);
    o->FieldRef("lowerBound").raw = f32(def.lowerBound);
    if (eventRanges) o->FieldRef("eventRanges").obj = eventRanges;
    return o;
}

// hkbBlendingTransitionEffect — the transition's effect node (referenced by hkbStateMachineTransitionInfo.
// m_transition). The typed builder always makes a blending effect. applySelfTransition/initializeCharacterPose
// are SERIALIZE_IGNORED but hky-authored; set them anyway (harmless to the byte gate, which skips ignored).
std::shared_ptr<io::SchemaObject> BuildTransitionEffect(const TransitionEffectDef& def, const schema::SchemaRegistry& reg) {
    auto o = make(reg, "hkbBlendingTransitionEffect");
    if (!o) return nullptr;
    o->FieldRef("userData").raw = leBytes(static_cast<std::uint64_t>(def.userData), 8);
    o->FieldRef("name").str     = def.name;
    o->FieldRef("selfTransitionMode").raw = leBytes(static_cast<std::uint64_t>(enums::ResolveEnum(def.selfTransitionMode, enums::SelfTransitionMode())), 1);
    o->FieldRef("eventMode").raw          = leBytes(static_cast<std::uint64_t>(enums::ResolveEnum(def.eventMode, enums::EventMode())), 1);
    o->FieldRef("duration").raw                     = f32(def.duration);
    o->FieldRef("toGeneratorStartTimeFraction").raw = f32(def.toGeneratorStartTimeFraction);
    o->FieldRef("flags").raw     = leBytes(static_cast<std::uint64_t>(enums::ResolveEnum(def.flags, enums::FlagBits())), 2);
    o->FieldRef("endMode").raw   = leBytes(static_cast<std::uint64_t>(enums::ResolveEnum(def.endMode, enums::EndMode())), 1);
    o->FieldRef("blendCurve").raw = leBytes(static_cast<std::uint64_t>(enums::ResolveEnum(def.blendCurve, enums::BlendCurve())), 1);
    if (o->HasField("applySelfTransition"))     o->FieldRef("applySelfTransition").raw     = leBytes(def.applySelfTransition ? 1u : 0u, 1);
    if (o->HasField("initializeCharacterPose")) o->FieldRef("initializeCharacterPose").raw = leBytes(def.initializeCharacterPose ? 1u : 0u, 1);
    if (def.bindings) { if (auto b = BuildBindingSet(*def.bindings, reg)) o->FieldRef("variableBindingSet").obj = b; }
    return o;
}

// hkbManualSelectorGenerator{ generators[] (edges), selectedGeneratorIndex, currentGeneratorIndex }.
std::shared_ptr<io::SchemaObject> BuildSelector(const ManualSelectorDef& def, const schema::SchemaRegistry& reg,
                                                const GenResolver& resolve) {
    auto o = make(reg, "hkbManualSelectorGenerator");
    if (!o) return nullptr;
    o->FieldRef("userData").raw = leBytes(static_cast<std::uint64_t>(def.userData), 8);
    o->FieldRef("name").str     = def.name;
    auto& gens = o->FieldRef("generators").objs;
    for (const auto& g : def.generators) if (auto n = resolve(g)) gens.push_back(n);
    o->FieldRef("selectedGeneratorIndex").raw = leBytes(static_cast<std::uint64_t>(static_cast<std::uint8_t>(static_cast<std::int8_t>(def.selectedGeneratorIndex))), 1);
    o->FieldRef("currentGeneratorIndex").raw  = leBytes(static_cast<std::uint64_t>(static_cast<std::uint8_t>(static_cast<std::int8_t>(def.currentGeneratorIndex))), 1);
    if (def.bindings) { if (auto b = BuildBindingSet(*def.bindings, reg)) o->FieldRef("variableBindingSet").obj = b; }
    return o;
}

std::shared_ptr<io::SchemaObject> BuildClip(const ClipGeneratorDef& def, const schema::SchemaRegistry& reg) {
    const schema::ClassSchema* cs = reg.Find("hkbClipGenerator");
    if (!cs) return nullptr;
    auto o = std::make_shared<io::SchemaObject>(&reg, cs);
    o->Init();   // complete default object; we overwrite the authored fields below

    // hkbNode (parent): userData + name.  (id/cloneState/padNode are ignored -> stay default.)
    o->FieldRef("userData").raw = leBytes(static_cast<std::uint64_t>(def.userData), 8);
    o->FieldRef("name").str     = def.name;

    // hkbClipGenerator (own scalar/enum fields — mirrors BehaviorBuilder::buildClip).
    o->FieldRef("animationName").str                  = def.animationName;
    o->FieldRef("cropStartAmountLocalTime").raw       = f32(def.cropStartAmountLocalTime);
    o->FieldRef("cropEndAmountLocalTime").raw         = f32(def.cropEndAmountLocalTime);
    o->FieldRef("startTime").raw                      = f32(def.startTime);
    o->FieldRef("playbackSpeed").raw                  = f32(def.playbackSpeed);
    o->FieldRef("enforcedDuration").raw               = f32(def.enforcedDuration);
    o->FieldRef("userControlledTimeFraction").raw     = f32(def.userControlledTimeFraction);
    o->FieldRef("animationBindingIndex").raw          = leBytes(static_cast<std::uint64_t>(static_cast<std::uint16_t>(static_cast<std::int16_t>(def.animationBindingIndex))), 2);
    o->FieldRef("mode").raw  = leBytes(static_cast<std::uint64_t>(enums::ResolveEnum(def.mode, enums::PlaybackMode())), 1);
    o->FieldRef("flags").raw = leBytes(static_cast<std::uint64_t>(def.flags), 1);

    // pointer sub-nodes (M2.1): triggers array + variable binding set.
    if (def.triggers) { if (auto t = BuildTriggers(*def.triggers, reg)) o->FieldRef("triggers").obj = t; }
    if (def.bindings) { if (auto b = BuildBindingSet(*def.bindings, reg)) o->FieldRef("variableBindingSet").obj = b; }
    return o;
}

} // namespace havok::model
