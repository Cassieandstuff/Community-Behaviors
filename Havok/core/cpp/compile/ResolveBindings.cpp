// ResolveBindings — the behavior-graph name->index bindings resolve pass, the compile-direction
// PREP that runs before the schema assembler (AssembleGraph) or the typed BehaviorBuilder emits.
// Pure BehaviorData/Def manipulation (event/variable/character-property name -> roster index) — NO
// typed hkb* classes. Extracted out of the quarantined havok-core's BehaviorBuilder.cpp into havok-model
// (the neutral model layer) so the schema compile path (Havok/core/cpp/compile) can reach it without a
// havok-core dependency; havok-core's typed builder + the offline gate still call the same one symbol.

#include <interface/BehaviorData.h>
#include <interface/HavokEnums.h>   // enums::ResolveEnum / BindingType (binding-kind resolve)

#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace havok::model {

namespace {

struct ResolveMaps { std::map<std::string, int> ev, var, prop; };

ResolveMaps buildResolveMaps(const BehaviorData& d) {
    ResolveMaps m;
    if (d.graphData) {
        const auto& gd = *d.graphData;
        for (int i = 0; i < static_cast<int>(gd.events.size()); ++i)
            m.ev[gd.events[i].name] = i;
        for (int i = 0; i < static_cast<int>(gd.variables.size()); ++i)
            m.var[gd.variables[i].name] = i;
        for (int i = 0; i < static_cast<int>(gd.characterPropertyNames.size()); ++i)
            m.prop[gd.characterPropertyNames[i].name] = i;
    }
    return m;
}

// mirror resolveVariableIndex (name path): named -> index, else throw.
int resolveVar(const std::string& name, const ResolveMaps& m) {
    auto it = m.var.find(name);
    if (it != m.var.end()) return it->second;
    throw std::runtime_error("BehaviorBuilder: unknown variable name '" + name + "'");
}
// Character-property NAME -> index: charprop roster, else the misextracted-variable roster
// (some char-prop bindings were recorded under a variable name). A name in NEITHER is a real
// dangling reference — THROW (mirror resolveVar/resolveEvent) instead of silently baking the stale
// numeric `fallback`, which would point at the wrong property (B6). `fallback` is unused now but
// kept in the signature for call-site symmetry.
int resolveProp(const std::string& name, int fallback, const ResolveMaps& m) {
    (void)fallback;
    auto it = m.prop.find(name); if (it != m.prop.end()) return it->second;
    auto v  = m.var.find(name);  if (v  != m.var.end())  return v->second;
    throw std::runtime_error("BehaviorBuilder: unknown character-property name '" + name +
        "' (not in the character-property or variable roster) — refusing to bake a stale index (B6).");
}

void resolveBindingVec(std::optional<std::vector<BindingDef>>& bs, const ResolveMaps& m) {
    if (!bs) return;
    for (auto& b : *bs) {
        if (!b.variable) continue;   // numeric binding: leave for the builder's OOB-checked path
        const long bt = enums::ResolveEnum(b.bindingType, enums::BindingType());
        b.variableIndex = (bt == 1 /*CHARACTER_PROPERTY*/)
            ? resolveProp(*b.variable, b.variableIndex, m)
            : resolveVar(*b.variable, m);
        b.variable.reset();
    }
}

template <class Def>
void passMap(std::map<std::string, Def>& mp, const ResolveMaps& m) {
    for (auto& [k, d] : mp) resolveBindingVec(d.bindings, m);
}

// ── event / variable field pre-resolution (mirror resolveEventId / resolveVariableIndex, name path) ──
int resolveEvent(const std::string& name, const ResolveMaps& m) {
    auto it = m.ev.find(name);
    if (it != m.ev.end()) return it->second;
    throw std::runtime_error("BehaviorBuilder: unknown event name '" + name + "'");
}
// A single (name, id) event pair: named -> id + clear; numeric left for the builder.
void rEv(std::optional<std::string>& name, int& id, const ResolveMaps& m) {
    if (name) { id = resolveEvent(*name, m); name.reset(); }
}
// A single (name, index) VARIABLE pair (syncVariable / iStateVar / assignmentVariable).
void rVar(std::optional<std::string>& name, int& idx, const ResolveMaps& m) {
    if (name) { idx = resolveVar(*name, m); name.reset(); }
}
void rInline(InlineEventDef& e, const ResolveMaps& m)   { rEv(e.event, e.id, m); }
void rEvProp(EventPropertyDef& e, const ResolveMaps& m) { rEv(e.event, e.id, m); }
void rInterval(TransitionIntervalDef& iv, const ResolveMaps& m) {
    rEv(iv.enterEvent, iv.enterEventId, m);
    rEv(iv.exitEvent,  iv.exitEventId,  m);
}
void rTransition(TransitionInfoDef& t, const ResolveMaps& m) {
    rInterval(t.triggerInterval, m);
    rInterval(t.initiateInterval, m);
    rEv(t.event, t.eventId, m);
}
template <class T>
void rTransVec(std::optional<std::vector<T>>& v, const ResolveMaps& m) {
    if (v) for (auto& t : *v) rTransition(t, m);
}
template <class T>
void rEvPropVec(std::optional<std::vector<T>>& v, const ResolveMaps& m) {
    if (v) for (auto& e : *v) rEvProp(e, m);
}
void rTriggers(std::optional<std::vector<ClipTriggerDef>>& v, const ResolveMaps& m) {
    if (v) for (auto& t : *v) rEv(t.event, t.eventId, m);
}

} // namespace

void ResolveBehaviorBindings(BehaviorData& data) {
    const ResolveMaps m = buildResolveMaps(data);
    passMap(data.clips, m);
    passMap(data.blenders, m);
    for (auto& [k, bl] : data.blenders)               // nested: hkbBlenderGeneratorChild is bindable
        for (auto& c : bl.children) resolveBindingVec(c.bindings, m);
    passMap(data.selectors, m);
    passMap(data.stateMachines, m);
    passMap(data.states, m);
    passMap(data.transitionEffects, m);
    passMap(data.modifierGenerators, m);
    passMap(data.isActiveModifiers, m);
    passMap(data.stateTaggingGenerators, m);
    passMap(data.behaviorReferences, m);
    passMap(data.gamebryoSequences, m);
    passMap(data.modifierLists, m);
    passMap(data.cyclicBlendGenerators, m);
    passMap(data.eventDrivenModifiers, m);
    passMap(data.eventEveryNModifiers, m);
    passMap(data.genericModifiers, m);
    passMap(data.footIkControlsModifiers, m);
    passMap(data.evaluateExpressionModifiers, m);
    passMap(data.interpValueModifiers, m);
    passMap(data.eventsFromRangeModifiers, m);
    passMap(data.boneSwitchGenerators, m);
    for (auto& [k, bsw] : data.boneSwitchGenerators)  // nested bone-switch children
        if (bsw.children) for (auto& c : *bsw.children) resolveBindingVec(c.bindings, m);
    passMap(data.synchronizedClips, m);
    passMap(data.offsetAnimGenerators, m);
    passMap(data.poseMatchingGenerators, m);
    passMap(data.referencePoseGenerators, m);
    passMap(data.iStateManagerModifiers, m);
    passMap(data.footIkModifiers, m);

    // ── events + single-variable fields (mirror the 34 resolve* sites in Build) ──
    for (auto& [k, c] : data.clips)                       // clip triggers
        rTriggers(c.triggers, m);
    for (auto& [k, s] : data.states) {                   // state notify events + transitions
        rEvPropVec(s.enterNotifyEvents, m);
        rEvPropVec(s.exitNotifyEvents, m);
        rTransVec(s.parsedTransitions, m);
        rTransVec(s.entryTransitions, m);
    }
    for (auto& [k, sm] : data.stateMachines) {           // SM change/return/random/higher/lower + sync + wildcards
        rEv(sm.eventToSendWhenStateOrTransitionChangesEvent, sm.eventToSendWhenStateOrTransitionChangesId, m);
        rEv(sm.returnToPreviousStateEvent,       sm.returnToPreviousStateEventId,       m);
        rEv(sm.randomTransitionEvent,            sm.randomTransitionEventId,            m);
        rEv(sm.transitionToNextHigherStateEvent, sm.transitionToNextHigherStateEventId, m);
        rEv(sm.transitionToNextLowerStateEvent,  sm.transitionToNextLowerStateEventId,  m);
        rVar(sm.syncVariable, sm.syncVariableIndex, m);
        rTransVec(sm.parsedWildcardTransitions, m);
    }
    for (auto& [k, c] : data.cyclicBlendGenerators) {    // inline events
        rInline(c.eventToFreezeBlendValue, m);
        rInline(c.eventToCrossBlend, m);
    }
    for (auto& [k, e] : data.eventEveryNModifiers) {
        rInline(e.eventToCheckFor, m);
        rInline(e.eventToSend, m);
    }
    for (auto& [k, e] : data.eventDrivenModifiers) {
        rEv(e.activateEvent,   e.activateEventId,   m);
        rEv(e.deactivateEvent, e.deactivateEventId, m);
    }
    for (auto& [k, p] : data.poseMatchingGenerators) {
        rEv(p.startPlayingEvent,  p.startPlayingEventId,  m);
        rEv(p.startMatchingEvent, p.startMatchingEventId, m);
    }
    for (auto& [k, im] : data.iStateManagerModifiers)    // iStateVar is a VARIABLE
        rVar(im.iStateVariable, im.iStateVar, m);
    for (auto& [k, fk] : data.footIkControlsModifiers)   // per-leg ungrounded events
        if (fk.legs) for (auto& leg : *fk.legs) if (leg.ungroundedEvent) rInline(*leg.ungroundedEvent, m);
    for (auto& [k, fk] : data.footIkModifiers)
        for (auto& leg : fk.legs) if (leg.ungroundedEvent) rInline(*leg.ungroundedEvent, m);
    for (auto& [k, ex] : data.expressionDataArrays)      // assignmentVariable (VAR) + assignmentEvent (EVENT)
        for (auto& e : ex.expressionsData) {
            rVar(e.assignmentVariable, e.assignmentVariableIndex, m);
            rEv(e.assignmentEvent,     e.assignmentEventIndex,    m);
        }
    for (auto& [k, er] : data.eventRangeDataArrays)      // range events
        for (auto& e : er.eventData) rEv(e.event, e.eventId, m);
    for (auto& [k, gm] : data.genericModifiers)          // generic-modifier inline-event params
        for (auto& p : gm.extraParams)
            if (p.eventValue) rInline(*p.eventValue, m);
}

} // namespace havok::model
