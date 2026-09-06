// PatchConverter.cpp — Nemesis/Pandora patch -> merged binary hkx, object-level.
// See havok/sct/PatchConverter.h for the design; spec §4.4 (oracle) + §11 (converter).

#include "havok/sct/PatchConverter.h"

#include "havok/classes/Classes.h"
#include "havok/classes/gen/ClassesGen.h"
#include "havok/core/HavokRegistry.h"
#include "havok/core/PackFileDeserializer.h"
#include "havok/core/PackFileSerializer.h"
#include "havok/model/BashMerge.h"
#include "havok/model/HavokEnums.h"
#include "havok/sct/BehaviorDecompiler.h"   // DecompileNativeDelta / DecompileBehaviorTree
#include "havok/sct/TagfileOracle.h"
#include "havok/xml/Xml.h"

#include <algorithm>
#include <cstdint>
#include <set>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace fs = std::filesystem;

namespace havok::sct {
namespace {

// ── small string / scalar helpers ─────────────────────────────────────────────
std::string trim(std::string_view s) {
    std::size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return std::string(s.substr(b, e - b));
}
long asLong(std::string_view s, long def = 0) {
    const std::string t = trim(s);
    if (t.empty()) return def;
    // Base 10 explicitly: zero-padded ids ("0571") must be decimal, not octal.
    try { return std::stol(t, nullptr, 10); } catch (...) { return def; }
}
float asFloat(std::string_view s, float def = 0.f) {
    const std::string t = trim(s);
    if (t.empty()) return def;
    try { return std::stof(t); } catch (...) { return def; }
}
bool asBool(std::string_view s) { return trim(s) == "true"; }

// ── xml::Node param accessors (a patch <hkobject>'s <hkparam> children) ─────────
const xml::Node* prm(const xml::Node& o, std::string_view name) {
    for (const auto& c : o.children)
        if (c.tag == "hkparam" && c.attr("name") == name) return &c;
    return nullptr;
}
std::string prmText(const xml::Node& o, std::string_view name, std::string_view def = {}) {
    const xml::Node* p = prm(o, name);
    return p ? p->text : std::string(def);
}
// First inline <hkobject> under a param (an inline-struct field).
const xml::Node* firstObj(const xml::Node* p) {
    if (p) for (const auto& c : p->children) if (c.tag == "hkobject") return &c;
    return nullptr;
}
// All inline <hkobject> under a param (an array-of-structs field).
std::vector<const xml::Node*> objs(const xml::Node& o, std::string_view name) {
    std::vector<const xml::Node*> v;
    if (const xml::Node* p = prm(o, name))
        for (const auto& c : p->children) if (c.tag == "hkobject") v.push_back(&c);
    return v;
}
// A string array: the <hkcstring> children of a param.
std::vector<std::string> strArray(const xml::Node& o, std::string_view name) {
    std::vector<std::string> v;
    if (const xml::Node* p = prm(o, name))
        for (const auto& e : p->children) if (e.tag == "hkcstring") v.push_back(e.text);
    return v;
}
// Whitespace-separated bare floats in a param's text (attributeDefaults, quads).
std::vector<float> floatList(const xml::Node& o, std::string_view name) {
    std::vector<float> v;
    if (const xml::Node* p = prm(o, name)) {
        std::stringstream ss(p->text);
        std::string t;
        while (ss >> t) v.push_back(asFloat(t));
    }
    return v;
}

// ── Bash-style node merge (param-level, generic — no per-class code) ─────────────
// Extracted to havok/model/BashMerge.h so the runtime YAML loader shares this exact
// merge core (havok::merge::bashMerge / decideParam) — one rule, no drift. The
// deepEqual / isArrayParam / tokens / mergeArrayInto / findParam / PatchLayer /
// bashMerge names below come from there via this using-directive.
using namespace havok::merge;

// changedFields (the per-layer MOD_CODE delta) now lives in the shared havok::merge core
// (BashMerge.h) and is reached via the `using namespace havok::merge` above — the SAME
// definition the schema-path parseSourcesMerged uses, so both build PatchLayer.changed identically.

// ── conversion context ─────────────────────────────────────────────────────────
struct Ctx {
    std::function<std::shared_ptr<IHavokObject>(const std::string&)> ref;  // "#N"/"null" -> obj
    std::unordered_map<std::string, int> evtIdx;   // event name  -> index
    std::unordered_map<std::string, int> varIdx;   // variable name-> index
    PatchConvertResult* r = nullptr;
};

// A field that is either a raw int or a Nemesis symbol ($eventID[X]$ / $variableID[X]$).
long intOrSymbol(std::string_view raw, Ctx& c) {
    const std::string s = trim(raw);
    auto sym = [&](const char* pfx, std::size_t n, std::unordered_map<std::string, int>& m,
                   const char* kind) -> long {
        const std::size_t e = s.find(']', n);
        const std::string nm = s.substr(n, e == std::string::npos ? std::string::npos : e - n);
        auto it = m.find(nm);
        if (it != m.end()) { ++c.r->symbolsResolved; return it->second; }
        c.r->warnings.push_back(std::string("unresolved ") + kind + " symbol: " + nm);
        return -1;
    };
    if (s.rfind("$variableID[", 0) == 0) return sym("$variableID[", 12, c.varIdx, "variable");
    if (s.rfind("$eventID[", 0) == 0)    return sym("$eventID[", 9, c.evtIdx, "event");
    return asLong(s);
}

std::shared_ptr<IHavokObject> refField(const xml::Node& o, std::string_view name, Ctx& c) {
    const xml::Node* p = prm(o, name);
    if (!p) return nullptr;
    const std::string t = trim(p->text);
    if (t.empty() || t == "null") return nullptr;
    if (t[0] == '#') return c.ref(t.substr(1));
    return nullptr;
}
template <class T>
std::shared_ptr<T> refFieldAs(const xml::Node& o, std::string_view name, Ctx& c) {
    return std::dynamic_pointer_cast<T>(refField(o, name, c));
}

// ── inline-struct populators ────────────────────────────────────────────────────
void popInterval(hkbStateMachineTimeInterval& iv, const xml::Node* n, Ctx& c) {
    if (!n) return;
    // enter/exitEventId are EVENT references and are frequently SYMBOLIC in Nemesis
    // patches ($eventID[Name]$) — BFCO gates its attack-window transitions this way.
    // asLong can't parse the token and silently yields 0 (a valid but WRONG event
    // index), corrupting the transition's active window = the combo input window.
    // Resolve through intOrSymbol like every other event id (it handles raw ints too).
    iv.m_enterEventId = static_cast<std::int32_t>(intOrSymbol(prmText(*n, "enterEventId"), c));
    iv.m_exitEventId  = static_cast<std::int32_t>(intOrSymbol(prmText(*n, "exitEventId"), c));
    iv.m_enterTime    = asFloat(prmText(*n, "enterTime"));
    iv.m_exitTime     = asFloat(prmText(*n, "exitTime"));
}
void popEventBase(hkbEventBase& e, const xml::Node& n, Ctx& c) {
    e.m_id      = static_cast<std::int32_t>(intOrSymbol(prmText(n, "id"), c));
    e.m_payload = refFieldAs<hkbEventPayload>(n, "payload", c);
}
void popRole(hkbRoleAttribute& ra, const xml::Node* n) {
    using namespace havok::model::enums;
    if (!n) return;
    ra.m_role  = static_cast<std::int16_t>(ResolveEnum(prmText(*n, "role"), Role()));
    ra.m_flags = static_cast<std::int16_t>(ResolveEnum(prmText(*n, "flags"), RoleFlags()));
}
void popVarInfo(hkbVariableInfo& vi, const xml::Node& n) {
    using namespace havok::model::enums;
    popRole(vi.m_role, firstObj(prm(n, "role")));
    vi.m_type = static_cast<std::int8_t>(ResolveEnum(prmText(n, "type"), VariableType()));
}

// ── base-class populators (inheritance chains) ───────────────────────────────────
void popBindable(hkbBindable& b, const xml::Node& n, Ctx& c) {
    b.m_variableBindingSet = refFieldAs<hkbVariableBindingSet>(n, "variableBindingSet", c);
}
void popNode(hkbNode& node, const xml::Node& n, Ctx& c) {
    popBindable(node, n, c);
    node.m_userData = static_cast<std::uint64_t>(asLong(prmText(n, "userData")));
    node.m_name     = prmText(n, "name");
}
void popModifier(hkbModifier& m, const xml::Node& n, Ctx& c) {
    popNode(m, n, c);
    m.m_enable = asBool(prmText(n, "enable"));
}
// Array of pointer refs from a param's whitespace-separated #tokens.
template <class T>
std::vector<std::shared_ptr<T>> refList(const xml::Node& n, std::string_view name, Ctx& c) {
    std::vector<std::shared_ptr<T>> v;
    if (const xml::Node* p = prm(n, name)) {
        std::stringstream ss(p->text); std::string tok;
        while (ss >> tok) if (!tok.empty() && tok[0] == '#')
            if (auto o = std::dynamic_pointer_cast<T>(c.ref(tok.substr(1)))) v.push_back(o);
    }
    return v;
}

// ── per-class populate. Returns false if the class has no populator yet. ─────────
bool populate(const std::shared_ptr<IHavokObject>& obj, const xml::Node& n, Ctx& c) {
    using namespace havok::model::enums;
    const std::string cls = obj->ClassName();

    if (cls == "hkbBehaviorGraphStringData") {
        auto* o = static_cast<hkbBehaviorGraphStringData*>(obj.get());
        o->m_eventNames             = strArray(n, "eventNames");
        o->m_attributeNames         = strArray(n, "attributeNames");
        o->m_variableNames          = strArray(n, "variableNames");
        o->m_characterPropertyNames = strArray(n, "characterPropertyNames");
        return true;
    }
    if (cls == "hkbCharacterStringData") {
        auto* o = static_cast<hkbCharacterStringData*>(obj.get());
        o->m_deformableSkinNames               = strArray(n, "deformableSkinNames");
        o->m_rigidSkinNames                    = strArray(n, "rigidSkinNames");
        o->m_animationNames                    = strArray(n, "animationNames");
        o->m_animationFilenames                = strArray(n, "animationFilenames");
        o->m_characterPropertyNames            = strArray(n, "characterPropertyNames");
        o->m_retargetingSkeletonMapperFilenames = strArray(n, "retargetingSkeletonMapperFilenames");
        o->m_lodNames                          = strArray(n, "lodNames");
        o->m_mirroredSyncPointSubstringsA      = strArray(n, "mirroredSyncPointSubstringsA");
        o->m_mirroredSyncPointSubstringsB      = strArray(n, "mirroredSyncPointSubstringsB");
        o->m_name             = prmText(n, "name");
        o->m_rigName          = prmText(n, "rigName");
        o->m_ragdollName      = prmText(n, "ragdollName");
        o->m_behaviorFilename = prmText(n, "behaviorFilename");
        return true;
    }
    if (cls == "hkbVariableValueSet") {
        auto* o = static_cast<hkbVariableValueSet*>(obj.get());
        o->m_wordVariableValues.clear();
        for (const auto* e : objs(n, "wordVariableValues")) {
            hkbVariableValue v; v.m_value = static_cast<std::int32_t>(asLong(prmText(*e, "value")));
            o->m_wordVariableValues.push_back(v);
        }
        o->m_quadVariableValues.clear();
        const auto q = floatList(n, "quadVariableValues");
        for (std::size_t i = 0; i + 3 < q.size(); i += 4)
            o->m_quadVariableValues.push_back(Vector4{q[i], q[i + 1], q[i + 2], q[i + 3]});
        o->m_variantVariableValues.clear();
        return true;
    }
    if (cls == "hkbBehaviorGraphData") {
        auto* o = static_cast<hkbBehaviorGraphData*>(obj.get());
        o->m_attributeDefaults = floatList(n, "attributeDefaults");
        o->m_variableInfos.clear();
        for (const auto* e : objs(n, "variableInfos")) { hkbVariableInfo vi; popVarInfo(vi, *e); o->m_variableInfos.push_back(vi); }
        o->m_characterPropertyInfos.clear();
        for (const auto* e : objs(n, "characterPropertyInfos")) { hkbVariableInfo vi; popVarInfo(vi, *e); o->m_characterPropertyInfos.push_back(vi); }
        o->m_eventInfos.clear();
        for (const auto* e : objs(n, "eventInfos")) { hkbEventInfo ei; ei.m_flags = static_cast<std::uint32_t>(ResolveEnum(prmText(*e, "flags"), EventInfoFlags())); o->m_eventInfos.push_back(ei); }
        auto words = [&](const char* k, std::vector<hkbVariableValue>& dst) {
            dst.clear();
            for (const auto* e : objs(n, k)) { hkbVariableValue v; v.m_value = static_cast<std::int32_t>(asLong(prmText(*e, "value"))); dst.push_back(v); }
        };
        words("wordMinVariableValues", o->m_wordMinVariableValues);
        words("wordMaxVariableValues", o->m_wordMaxVariableValues);
        o->m_variableInitialValues = refFieldAs<hkbVariableValueSet>(n, "variableInitialValues", c);
        o->m_stringData            = refFieldAs<hkbBehaviorGraphStringData>(n, "stringData", c);
        return true;
    }
    if (cls == "hkbStateMachineEventPropertyArray") {
        auto* o = static_cast<hkbStateMachineEventPropertyArray*>(obj.get());
        o->m_events.clear();
        for (const auto* e : objs(n, "events")) { hkbEventProperty ep; popEventBase(ep, *e, c); o->m_events.push_back(ep); }
        return true;
    }
    if (cls == "hkbStateMachineTransitionInfoArray") {
        auto* o = static_cast<hkbStateMachineTransitionInfoArray*>(obj.get());
        o->m_transitions.clear();
        for (const auto* t : objs(n, "transitions")) {
            hkbStateMachineTransitionInfo ti;
            popInterval(ti.m_triggerInterval,  firstObj(prm(*t, "triggerInterval")),  c);
            popInterval(ti.m_initiateInterval, firstObj(prm(*t, "initiateInterval")), c);
            ti.m_transition        = refFieldAs<hkbTransitionEffect>(*t, "transition", c);
            ti.m_condition         = refFieldAs<hkbCondition>(*t, "condition", c);
            ti.m_eventId           = static_cast<std::int32_t>(intOrSymbol(prmText(*t, "eventId"), c));
            ti.m_toStateId         = static_cast<std::int32_t>(asLong(prmText(*t, "toStateId")));
            ti.m_fromNestedStateId = static_cast<std::int32_t>(asLong(prmText(*t, "fromNestedStateId")));
            ti.m_toNestedStateId   = static_cast<std::int32_t>(asLong(prmText(*t, "toNestedStateId")));
            ti.m_priority          = static_cast<std::int16_t>(asLong(prmText(*t, "priority")));
            ti.m_flags             = static_cast<std::int16_t>(ResolveEnum(prmText(*t, "flags"), TransitionFlags()));
            o->m_transitions.push_back(ti);
        }
        return true;
    }
    if (cls == "hkbVariableBindingSet") {
        auto* o = static_cast<hkbVariableBindingSet*>(obj.get());
        o->m_bindings.clear();
        for (const auto* b : objs(n, "bindings")) {
            hkbVariableBindingSetBinding vb;
            vb.m_memberPath    = prmText(*b, "memberPath");
            vb.m_variableIndex = static_cast<std::int32_t>(intOrSymbol(prmText(*b, "variableIndex"), c));
            vb.m_bitIndex      = static_cast<std::int8_t>(asLong(prmText(*b, "bitIndex")));
            vb.m_bindingType   = static_cast<std::int8_t>(ResolveEnum(prmText(*b, "bindingType"), BindingType()));
            o->m_bindings.push_back(vb);
        }
        o->m_indexOfBindingToEnable = static_cast<std::int32_t>(asLong(prmText(n, "indexOfBindingToEnable")));
        return true;
    }
    if (cls == "hkbStateMachineStateInfo") {
        auto* o = static_cast<hkbStateMachineStateInfo*>(obj.get());
        popBindable(*o, n, c);
        o->m_listeners.clear();
        o->m_enterNotifyEvents = refFieldAs<hkbStateMachineEventPropertyArray>(n, "enterNotifyEvents", c);
        o->m_exitNotifyEvents  = refFieldAs<hkbStateMachineEventPropertyArray>(n, "exitNotifyEvents", c);
        o->m_transitions       = refFieldAs<hkbStateMachineTransitionInfoArray>(n, "transitions", c);
        o->m_generator         = refFieldAs<hkbGenerator>(n, "generator", c);
        o->m_name        = prmText(n, "name");
        o->m_stateId     = static_cast<std::int32_t>(asLong(prmText(n, "stateId")));
        o->m_probability = asFloat(prmText(n, "probability"));
        o->m_enable      = asBool(prmText(n, "enable"));
        return true;
    }
    if (cls == "hkbStateMachine") {
        auto* o = static_cast<hkbStateMachine*>(obj.get());
        popNode(*o, n, c);
        popEventBase(o->m_eventToSendWhenStateOrTransitionChanges,
                     *firstObj(prm(n, "eventToSendWhenStateOrTransitionChanges")), c);
        o->m_startStateChooser                  = refFieldAs<hkbStateChooser>(n, "startStateChooser", c);
        o->m_startStateId                       = static_cast<std::int32_t>(asLong(prmText(n, "startStateId")));
        o->m_returnToPreviousStateEventId       = static_cast<std::int32_t>(intOrSymbol(prmText(n, "returnToPreviousStateEventId"), c));
        o->m_randomTransitionEventId            = static_cast<std::int32_t>(intOrSymbol(prmText(n, "randomTransitionEventId"), c));
        o->m_transitionToNextHigherStateEventId = static_cast<std::int32_t>(intOrSymbol(prmText(n, "transitionToNextHigherStateEventId"), c));
        o->m_transitionToNextLowerStateEventId  = static_cast<std::int32_t>(intOrSymbol(prmText(n, "transitionToNextLowerStateEventId"), c));
        o->m_syncVariableIndex                  = static_cast<std::int32_t>(intOrSymbol(prmText(n, "syncVariableIndex"), c));
        o->m_wrapAroundStateId                  = asBool(prmText(n, "wrapAroundStateId"));
        o->m_maxSimultaneousTransitions         = static_cast<std::int8_t>(asLong(prmText(n, "maxSimultaneousTransitions")));
        o->m_startStateMode                     = static_cast<std::int8_t>(ResolveEnum(prmText(n, "startStateMode"), StartStateMode()));
        o->m_selfTransitionMode                 = static_cast<std::int8_t>(ResolveEnum(prmText(n, "selfTransitionMode"), SmSelfTransitionMode()));
        o->m_states.clear();
        if (const xml::Node* p = prm(n, "states")) {
            std::stringstream ss(p->text); std::string tok;
            while (ss >> tok) if (!tok.empty() && tok[0] == '#')
                if (auto s = std::dynamic_pointer_cast<hkbStateMachineStateInfo>(c.ref(tok.substr(1)))) o->m_states.push_back(s);
        }
        o->m_wildcardTransitions = refFieldAs<hkbStateMachineTransitionInfoArray>(n, "wildcardTransitions", c);
        return true;
    }
    if (cls == "hkbClipGenerator") {
        auto* o = static_cast<hkbClipGenerator*>(obj.get());
        popNode(*o, n, c);
        o->m_animationName              = prmText(n, "animationName");
        o->m_triggers                   = refFieldAs<hkbClipTriggerArray>(n, "triggers", c);
        o->m_cropStartAmountLocalTime   = asFloat(prmText(n, "cropStartAmountLocalTime"));
        o->m_cropEndAmountLocalTime     = asFloat(prmText(n, "cropEndAmountLocalTime"));
        o->m_startTime                  = asFloat(prmText(n, "startTime"));
        o->m_playbackSpeed              = asFloat(prmText(n, "playbackSpeed"));
        o->m_enforcedDuration           = asFloat(prmText(n, "enforcedDuration"));
        o->m_userControlledTimeFraction = asFloat(prmText(n, "userControlledTimeFraction"));
        o->m_animationBindingIndex      = static_cast<std::int16_t>(asLong(prmText(n, "animationBindingIndex")));
        o->m_mode                       = static_cast<std::int8_t>(ResolveEnum(prmText(n, "mode"), PlaybackMode()));
        o->m_flags                      = static_cast<std::int8_t>(ResolveEnum(prmText(n, "flags"), ClipGeneratorFlags()));
        return true;
    }
    if (cls == "hkbClipTriggerArray") {
        auto* o = static_cast<hkbClipTriggerArray*>(obj.get());
        o->m_triggers.clear();
        for (const auto* t : objs(n, "triggers")) {
            hkbClipTrigger tr;
            tr.m_localTime = asFloat(prmText(*t, "localTime"));
            if (const xml::Node* ev = firstObj(prm(*t, "event"))) popEventBase(tr.m_event, *ev, c);
            tr.m_relativeToEndOfClip = asBool(prmText(*t, "relativeToEndOfClip"));
            tr.m_acyclic             = asBool(prmText(*t, "acyclic"));
            tr.m_isAnnotation        = asBool(prmText(*t, "isAnnotation"));
            o->m_triggers.push_back(tr);
        }
        return true;
    }
    if (cls == "hkbManualSelectorGenerator") {
        auto* o = static_cast<hkbManualSelectorGenerator*>(obj.get());
        popNode(*o, n, c);
        o->m_generators            = refList<hkbGenerator>(n, "generators", c);
        o->m_selectedGeneratorIndex = static_cast<std::int8_t>(asLong(prmText(n, "selectedGeneratorIndex")));
        o->m_currentGeneratorIndex  = static_cast<std::int8_t>(asLong(prmText(n, "currentGeneratorIndex")));
        return true;
    }
    if (cls == "hkbModifierGenerator") {
        auto* o = static_cast<hkbModifierGenerator*>(obj.get());
        popNode(*o, n, c);
        o->m_modifier  = refFieldAs<hkbModifier>(n, "modifier", c);
        o->m_generator = refFieldAs<hkbGenerator>(n, "generator", c);
        return true;
    }
    if (cls == "hkbBehaviorReferenceGenerator") {
        auto* o = static_cast<hkbBehaviorReferenceGenerator*>(obj.get());
        popNode(*o, n, c);   // variableBindingSet + userData + name
        o->m_behaviorName = prmText(n, "behaviorName");   // e.g. "behaviors\skyparkour_behavior.hkx"
        return true;
    }
    if (cls == "BSiStateTaggingGenerator") {
        // Without this populator the node is emitted BLANK — crucially m_pDefaultGenerator (the wrapped
        // child generator) stays null, and char-setup virtual-calls that null generator → CRASH (BR-10).
        auto* o = static_cast<BSiStateTaggingGenerator*>(obj.get());
        popNode(*o, n, c);   // variableBindingSet + userData + name
        o->m_pDefaultGenerator = refFieldAs<hkbGenerator>(n, "pDefaultGenerator", c);
        o->m_iStateToSetAs     = static_cast<std::int32_t>(asLong(prmText(n, "iStateToSetAs")));
        o->m_iPriority         = static_cast<std::int32_t>(asLong(prmText(n, "iPriority")));
        return true;
    }
    if (cls == "hkbModifierList") {
        auto* o = static_cast<hkbModifierList*>(obj.get());
        popModifier(*o, n, c);
        o->m_modifiers = refList<hkbModifier>(n, "modifiers", c);
        return true;
    }
    if (cls == "hkbEventDrivenModifier") {
        auto* o = static_cast<hkbEventDrivenModifier*>(obj.get());
        popModifier(*o, n, c);
        o->m_modifier          = refFieldAs<hkbModifier>(n, "modifier", c);  // hkbModifierWrapper base
        o->m_activateEventId   = static_cast<std::int32_t>(intOrSymbol(prmText(n, "activateEventId"), c));
        o->m_deactivateEventId = static_cast<std::int32_t>(intOrSymbol(prmText(n, "deactivateEventId"), c));
        o->m_activeByDefault   = asBool(prmText(n, "activeByDefault"));
        return true;
    }
    if (cls == "BSIsActiveModifier") {
        auto* o = static_cast<BSIsActiveModifier*>(obj.get());
        popModifier(*o, n, c);
        o->m_bIsActive0 = asBool(prmText(n, "bIsActive0")); o->m_bInvertActive0 = asBool(prmText(n, "bInvertActive0"));
        o->m_bIsActive1 = asBool(prmText(n, "bIsActive1")); o->m_bInvertActive1 = asBool(prmText(n, "bInvertActive1"));
        o->m_bIsActive2 = asBool(prmText(n, "bIsActive2")); o->m_bInvertActive2 = asBool(prmText(n, "bInvertActive2"));
        o->m_bIsActive3 = asBool(prmText(n, "bIsActive3")); o->m_bInvertActive3 = asBool(prmText(n, "bInvertActive3"));
        o->m_bIsActive4 = asBool(prmText(n, "bIsActive4")); o->m_bInvertActive4 = asBool(prmText(n, "bInvertActive4"));
        return true;
    }
    if (cls == "hkbEvaluateExpressionModifier") {
        auto* o = static_cast<hkbEvaluateExpressionModifier*>(obj.get());
        popModifier(*o, n, c);
        o->m_expressions = refFieldAs<hkbExpressionDataArray>(n, "expressions", c);
        return true;
    }
    if (cls == "hkbExpressionDataArray") {
        auto* o = static_cast<hkbExpressionDataArray*>(obj.get());
        o->m_expressionsData.clear();
        for (const auto* e : objs(n, "expressionsData")) {
            hkbExpressionData ed;
            ed.m_expression              = prmText(*e, "expression");
            ed.m_assignmentVariableIndex = static_cast<std::int32_t>(intOrSymbol(prmText(*e, "assignmentVariableIndex"), c));
            ed.m_assignmentEventIndex    = static_cast<std::int32_t>(intOrSymbol(prmText(*e, "assignmentEventIndex"), c));
            ed.m_eventMode               = static_cast<std::int8_t>(ResolveEnum(prmText(*e, "eventMode"), ExpressionEventMode()));
            o->m_expressionsData.push_back(ed);
        }
        return true;
    }
    if (cls == "hkbExpressionCondition") {
        auto* o = static_cast<hkbExpressionCondition*>(obj.get());
        o->m_expression = prmText(n, "expression");
        return true;
    }
    if (cls == "hkbStringEventPayload") {
        auto* o = static_cast<hkbStringEventPayload*>(obj.get());
        o->m_data = prmText(n, "data");
        return true;
    }
    if (cls == "hkbBoneWeightArray") {
        auto* o = static_cast<hkbBoneWeightArray*>(obj.get());
        popBindable(*o, n, c);
        o->m_boneWeights = floatList(n, "boneWeights");
        return true;
    }
    if (cls == "hkbBlenderGeneratorChild") {
        auto* o = static_cast<hkbBlenderGeneratorChild*>(obj.get());
        popBindable(*o, n, c);   // variableBindingSet — the original iceskating field
        o->m_generator            = refFieldAs<hkbGenerator>(n, "generator", c);
        o->m_boneWeights          = refFieldAs<hkbBoneWeightArray>(n, "boneWeights", c);
        o->m_weight               = asFloat(prmText(n, "weight"));
        o->m_worldFromModelWeight = asFloat(prmText(n, "worldFromModelWeight"));
        return true;
    }
    if (cls == "hkbBlenderGenerator") {
        auto* o = static_cast<hkbBlenderGenerator*>(obj.get());
        popNode(*o, n, c);
        o->m_referencePoseWeightThreshold = asFloat(prmText(n, "referencePoseWeightThreshold"));
        o->m_blendParameter               = asFloat(prmText(n, "blendParameter"));
        o->m_minCyclicBlendParameter      = asFloat(prmText(n, "minCyclicBlendParameter"));
        o->m_maxCyclicBlendParameter      = asFloat(prmText(n, "maxCyclicBlendParameter"));
        o->m_indexOfSyncMasterChild       = static_cast<std::int16_t>(asLong(prmText(n, "indexOfSyncMasterChild")));
        o->m_flags                        = static_cast<std::int16_t>(ResolveEnum(prmText(n, "flags"), BlenderFlags()));
        o->m_subtractLastChild            = asBool(prmText(n, "subtractLastChild"));
        o->m_children                     = refList<hkbBlenderGeneratorChild>(n, "children", c);
        return true;
    }
    if (cls == "hkbTwistModifier") {
        auto* o = static_cast<hkbTwistModifier*>(obj.get());
        popModifier(*o, n, c);
        const auto ax = floatList(n, "axisOfRotation");
        if (ax.size() >= 4) o->m_axisOfRotation = Vector4{ax[0], ax[1], ax[2], ax[3]};
        o->m_twistAngle             = asFloat(prmText(n, "twistAngle"));
        o->m_startBoneIndex         = static_cast<std::int16_t>(asLong(prmText(n, "startBoneIndex")));
        o->m_endBoneIndex           = static_cast<std::int16_t>(asLong(prmText(n, "endBoneIndex")));
        o->m_setAngleMethod         = static_cast<std::int8_t>(ResolveEnum(prmText(n, "setAngleMethod"), SetAngleMethod()));
        o->m_rotationAxisCoordinates = static_cast<std::int8_t>(ResolveEnum(prmText(n, "rotationAxisCoordinates"), RotationAxisCoordinates()));
        o->m_isAdditive             = asBool(prmText(n, "isAdditive"));
        auto int16s = [&](const char* k) { std::vector<std::int16_t> v; if (const xml::Node* p = prm(n, k)) for (const auto& t : tokens(p->text)) v.push_back(static_cast<std::int16_t>(asLong(t))); return v; };
        o->m_boneChainIndices  = int16s("boneChainIndices");
        o->m_parentBoneIndices = int16s("parentBoneIndices");
        return true;
    }
    if (cls == "hkbTimerModifier") {
        auto* o = static_cast<hkbTimerModifier*>(obj.get());
        popModifier(*o, n, c);
        o->m_alarmTimeSeconds = asFloat(prmText(n, "alarmTimeSeconds"));
        if (const xml::Node* ev = firstObj(prm(n, "alarmEvent"))) popEventBase(o->m_alarmEvent, *ev, c);
        return true;
    }
    if (cls == "hkbBlendingTransitionEffect") {
        auto* o = static_cast<hkbBlendingTransitionEffect*>(obj.get());
        popNode(*o, n, c);  // hkbTransitionEffect : hkbGenerator : hkbNode
        o->m_selfTransitionMode        = static_cast<std::int8_t>(ResolveEnum(prmText(n, "selfTransitionMode"), SelfTransitionMode()));
        o->m_eventMode                 = static_cast<std::int8_t>(ResolveEnum(prmText(n, "eventMode"), EventMode()));
        o->m_duration                  = asFloat(prmText(n, "duration"));
        o->m_toGeneratorStartTimeFraction = asFloat(prmText(n, "toGeneratorStartTimeFraction"));
        o->m_flags                     = static_cast<std::uint16_t>(ResolveEnum(prmText(n, "flags"), FlagBits()));
        o->m_endMode                   = static_cast<std::int8_t>(ResolveEnum(prmText(n, "endMode"), EndMode()));
        o->m_blendCurve                = static_cast<std::int8_t>(ResolveEnum(prmText(n, "blendCurve"), BlendCurve()));
        return true;
    }
    return false;  // no populator for this class yet
}

std::vector<std::uint8_t> readFile(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

// Merge the graph's symbol tables — variables, events, character-properties — as
// ALIGNED tuples keyed by NAME, then write the parallel arrays back in lockstep. A
// variable is spread across three nodes (name in stringData; type/min/max in
// graphData; initial value in variableValueSet); merging each array independently
// lets them desync (name #123 = MCO_nextattack but type #123 = some bool), which
// silently corrupts every index-addressed reference. Building one {name,info,val,
// min,max} record per variable makes desync impossible by construction — the BDI
// model. Vanilla entries first (indices preserved), each mod's NEW names appended in
// load order. Sources are re-read per patch dir so the three nodes stay correlated
// to the same mod.
void mergeSymbols(const std::vector<std::string>& patchDirs,
                  std::uint32_t sdId, std::uint32_t gdId, std::uint32_t vsId,
                  const xml::Node* vSd, const xml::Node* vGd, const xml::Node* vVs,
                  hkbBehaviorGraphStringData* sd, hkbBehaviorGraphData* gd,
                  hkbVariableValueSet* vs) {
    struct VarRec  { std::string name; hkbVariableInfo info; hkbVariableValue val, mn, mx; };
    struct EvtRec  { std::string name; hkbEventInfo info; };
    struct PropRec { std::string name; hkbVariableInfo info; };
    std::vector<VarRec> vars; std::vector<EvtRec> evts; std::vector<PropRec> props;
    std::unordered_set<std::string> vSeen, eSeen, pSeen;

    // The per-symbol info (hkbVariableInfo — hkbRoleAttribute role+flags, type; and the
    // event hkbEventInfo flags) is AUTHORITATIVE from the binary read. The vanilla XML
    // tagfile templates are lossy here: they dump each hkbRoleAttribute's high bits as a
    // `<!-- UNKNOWN BITS -->0xNNNN` comment (which ResolveEnum then drops) and can even
    // mis-decompose the named bits — e.g. mt_behavior's IsMale/UpperBody character-property
    // role flags (5804 / 5849) both collapse to the default 37. Snapshot the binary infos
    // by name NOW (sd/gd still hold the vanilla arrays; the addSource pass below clears and
    // rebuilds them from XML), then restore them onto the rebuilt vanilla entries. This
    // matches the existing "vanilla wins" merge rule — only the SOURCE of the vanilla value
    // shifts from lossy-XML to the binary. Mod-added symbols are absent from these maps and
    // keep their XML-derived info.
    std::unordered_map<std::string, hkbVariableInfo> binVarInfo, binPropInfo;
    std::unordered_map<std::string, hkbEventInfo>    binEvtInfo;
    for (std::size_t i = 0; i < sd->m_variableNames.size() && i < gd->m_variableInfos.size(); ++i)
        binVarInfo.emplace(sd->m_variableNames[i], gd->m_variableInfos[i]);
    for (std::size_t i = 0; i < sd->m_eventNames.size() && i < gd->m_eventInfos.size(); ++i)
        binEvtInfo.emplace(sd->m_eventNames[i], gd->m_eventInfos[i]);
    for (std::size_t i = 0; i < sd->m_characterPropertyNames.size() && i < gd->m_characterPropertyInfos.size(); ++i)
        binPropInfo.emplace(sd->m_characterPropertyNames[i], gd->m_characterPropertyInfos[i]);

    auto wordVal = [](const xml::Node* n) {
        hkbVariableValue v; if (n) v.m_value = static_cast<std::int32_t>(asLong(prmText(*n, "value"))); return v;
    };
    // Names may be sourced from the authoritative BINARY arrays instead of the XML: the
    // tagfile XML parser trims surrounding whitespace on element text (Xml.h), so a name
    // that carries significant leading/trailing whitespace (vanilla's ` iState_NPCSneaking`
    // typo, distinct from `iState_NPCSneaking`) arrives trimmed, collides in `vSeen`, and is
    // wrongly dropped — shifting every later index and breaking by-index bindings on recompile
    // (BR-5: `variableIndex: 80` OOB in magic{,mounted}behavior). The binary read is faithful,
    // so the VANILLA pass passes bin* names; mod deltas still come from XML (bin* == nullptr).
    // XML infos/values stay index-aligned (vanilla XML mirrors binary order); binVarInfo below
    // re-restores infos by name regardless.
    auto addSource = [&](const xml::Node* xsd, const xml::Node* xgd, const xml::Node* xvs,
                         const std::vector<std::string>* binVarNames  = nullptr,
                         const std::vector<std::string>* binEvtNames  = nullptr,
                         const std::vector<std::string>* binPropNames = nullptr) {
        if (!xsd) return;
        const std::vector<const xml::Node*> none;
        const auto xmlVarNames = strArray(*xsd, "variableNames");
        const auto& names = binVarNames ? *binVarNames : xmlVarNames;
        const auto infos = xgd ? objs(*xgd, "variableInfos")          : none;
        const auto mins  = xgd ? objs(*xgd, "wordMinVariableValues")  : none;
        const auto maxs  = xgd ? objs(*xgd, "wordMaxVariableValues")  : none;
        const auto vals  = xvs ? objs(*xvs, "wordVariableValues")     : none;
        for (std::size_t j = 0; j < names.size(); ++j) {
            if (!vSeen.insert(names[j]).second) continue;   // first source (vanilla, then load order) wins
            VarRec rc; rc.name = names[j];
            if (j < infos.size()) popVarInfo(rc.info, *infos[j]);
            if (j < vals.size())  rc.val = wordVal(vals[j]);
            if (j < mins.size())  rc.mn  = wordVal(mins[j]);
            if (j < maxs.size())  rc.mx  = wordVal(maxs[j]);
            vars.push_back(std::move(rc));
        }
        const auto xmlEvtNames = strArray(*xsd, "eventNames");
        const auto& en = binEvtNames ? *binEvtNames : xmlEvtNames;
        const auto ei = xgd ? objs(*xgd, "eventInfos") : none;
        for (std::size_t j = 0; j < en.size(); ++j) {
            if (!eSeen.insert(en[j]).second) continue;
            EvtRec rc; rc.name = en[j];
            if (j < ei.size()) rc.info.m_flags = static_cast<std::uint32_t>(
                havok::model::enums::ResolveEnum(prmText(*ei[j], "flags"), havok::model::enums::EventInfoFlags()));
            evts.push_back(std::move(rc));
        }
        const auto xmlPropNames = strArray(*xsd, "characterPropertyNames");
        const auto& pn = binPropNames ? *binPropNames : xmlPropNames;
        const auto pi = xgd ? objs(*xgd, "characterPropertyInfos") : none;
        for (std::size_t j = 0; j < pn.size(); ++j) {
            if (!pSeen.insert(pn[j]).second) continue;
            PropRec rc; rc.name = pn[j];
            if (j < pi.size()) popVarInfo(rc.info, *pi[j]);
            props.push_back(std::move(rc));
        }
    };

    // vanilla first — names from the authoritative binary arrays (see note above)
    addSource(vSd, vGd, vVs, &sd->m_variableNames, &sd->m_eventNames, &sd->m_characterPropertyNames);
    const std::size_t baseV = vars.size(), baseE = evts.size(), baseP = props.size();
    for (const std::string& dir : patchDirs) {        // then each mod, load order
        auto rd = [&](std::uint32_t id) -> xml::Node {
            char b[16]; std::snprintf(b, sizeof b, "#%04u.txt", id);
            const fs::path f = fs::path(dir) / b;
            std::error_code ec; if (!fs::exists(f, ec)) return {};
            auto raw = readFile(f.string()); std::string s(raw.begin(), raw.end());
            xml::StripPatchOriginals(s); return xml::Parse(s);
        };
        xml::Node ns = rd(sdId), ng = rd(gdId), nv = rd(vsId);
        addSource(ns.tag.empty() ? nullptr : &ns, ng.tag.empty() ? nullptr : &ng,
                  nv.tag.empty() ? nullptr : &nv);
    }

    // Order fix (matches Pandora): mod-added symbols are appended in the REVERSE of
    // load order — the highest-priority mod's new symbols come first, right after the
    // untouched vanilla block. We accumulate the tail low-priority-first above, so
    // reverse just the appended range [base..end). Identity is by name and each record
    // carries its own info/value, so reversing positions cannot desync
    // name<->info<->value. This symbol order is LOAD-BEARING: Pandora emits the
    // shipped animation{,set}datasinglefile keyed to it, and downstream every
    // $eventID/$variableID reference is re-resolved from this exact table
    // (ctx.evtIdx/varIdx are rebuilt from it), so the whole graph stays self-consistent.
    std::reverse(vars.begin()  + static_cast<std::ptrdiff_t>(baseV), vars.end());
    std::reverse(evts.begin()  + static_cast<std::ptrdiff_t>(baseE), evts.end());
    std::reverse(props.begin() + static_cast<std::ptrdiff_t>(baseP), props.end());

    // Write the aligned arrays back in lockstep.
    sd->m_variableNames.clear(); sd->m_eventNames.clear(); sd->m_characterPropertyNames.clear();
    gd->m_variableInfos.clear(); gd->m_wordMinVariableValues.clear();
    gd->m_wordMaxVariableValues.clear(); gd->m_eventInfos.clear(); gd->m_characterPropertyInfos.clear();
    vs->m_wordVariableValues.clear();
    for (auto& v : vars) {
        sd->m_variableNames.push_back(v.name);
        gd->m_variableInfos.push_back(v.info);
        gd->m_wordMinVariableValues.push_back(v.mn);
        gd->m_wordMaxVariableValues.push_back(v.mx);
        vs->m_wordVariableValues.push_back(v.val);
    }
    for (auto& e : evts) { sd->m_eventNames.push_back(e.name); gd->m_eventInfos.push_back(e.info); }
    for (auto& p : props) { sd->m_characterPropertyNames.push_back(p.name); gd->m_characterPropertyInfos.push_back(p.info); }

    // Restore the authoritative binary infos onto every vanilla symbol (see snapshot above).
    // Names now match the rebuilt arrays 1:1; a mod-added symbol simply misses the map.
    for (std::size_t i = 0; i < sd->m_variableNames.size() && i < gd->m_variableInfos.size(); ++i)
        if (auto it = binVarInfo.find(sd->m_variableNames[i]); it != binVarInfo.end())
            gd->m_variableInfos[i] = it->second;
    for (std::size_t i = 0; i < sd->m_eventNames.size() && i < gd->m_eventInfos.size(); ++i)
        if (auto it = binEvtInfo.find(sd->m_eventNames[i]); it != binEvtInfo.end())
            gd->m_eventInfos[i] = it->second;
    for (std::size_t i = 0; i < sd->m_characterPropertyNames.size() && i < gd->m_characterPropertyInfos.size(); ++i)
        if (auto it = binPropInfo.find(sd->m_characterPropertyNames[i]); it != binPropInfo.end())
            gd->m_characterPropertyInfos[i] = it->second;
}

}  // namespace

PatchConvertResult ConvertPatch(const std::string& vanillaBin, const std::string& vanillaXml,
                                const std::vector<std::string>& patchDirs, const std::string& outBin,
                                const std::string& nativeDeltaDir, const std::string& vanBaseDir) {
    PatchConvertResult r;

    // 1. Deserialize vanilla fully from root -> live object graph.
    std::vector<std::uint8_t> bytes = readFile(vanillaBin);
    if (bytes.empty()) { r.error = "cannot read vanilla binary: " + vanillaBin; return r; }
    PackFileDeserializer des;
    BinaryReaderEx br(false, true, bytes);
    des.DeserializePartially(br);
    std::unordered_map<std::uint32_t, std::string> binClass;
    std::uint32_t binRoot = 0xFFFFFFFFu;
    for (const auto& [o, cn] : des.ListObjects()) { binClass[o] = cn; if (cn == "hkRootLevelContainer") binRoot = o; }
    if (binRoot == 0xFFFFFFFFu) { r.error = "no root in vanilla binary"; return r; }
    BinaryReaderEx dr(des._header.Endian == 0, des._header.PointerSize == 8, des.DataSectionBytes());
    std::shared_ptr<IHavokObject> root;
    try { root = des.ConstructVirtualClass(dr, binRoot); }
    catch (const std::exception& e) { r.error = std::string("construct: ") + e.what(); return r; }
    const auto& binRefs = des.RefsInReadOrder();

    // 2. Oracle: co-DFS the binary graph against the vanilla tagfile -> offset -> #NNNN
    //    (shared, proven path — see havok/sct/TagfileOracle.h).
    (void)binRefs;
    std::string xmlSrc; { auto b = readFile(vanillaXml); xmlSrc.assign(b.begin(), b.end()); }
    if (xmlSrc.empty()) { r.error = "cannot read vanilla tagfile: " + vanillaXml; return r; }
    const OracleResult oracle = AlignTagfile(des, xmlSrc);
    if (!oracle.ok) { r.error = "oracle: " + oracle.error; return r; }
    if (oracle.classMism > 0)
        r.warnings.push_back("oracle: " + std::to_string(oracle.classMism) +
                             " class mismatch(es) — vanilla binary/tagfile may not match");
    // #NNNN -> live object (vanilla; overrides mutate these in place).
    std::unordered_map<std::uint32_t, std::shared_ptr<IHavokObject>> byNum;
    for (const auto& [off, obj] : des.DeserializedObjects()) {
        auto it = oracle.off2id.find(off);
        if (it != oracle.off2id.end()) byNum[it->second] = obj;
    }

    // The graph's single symbol nodes — their parallel arrays are merged as aligned
    // tuples (mergeSymbols), not by the generic per-array union which desyncs them.
    std::uint32_t sdId = 0, gdId = 0, vsId = 0;
    for (const auto& [id, obj] : byNum) {
        if (!sdId && std::dynamic_pointer_cast<hkbBehaviorGraphStringData>(obj)) sdId = id;
        else if (!gdId && std::dynamic_pointer_cast<hkbBehaviorGraphData>(obj)) gdId = id;
        else if (!vsId && std::dynamic_pointer_cast<hkbVariableValueSet>(obj)) vsId = id;
    }

    // 2b. Parse the vanilla tagfile into per-#NNNN nodes — the base the bashed merge
    //     diffs each mod's override against (so only real deltas apply).
    std::unordered_map<std::uint32_t, xml::Node> vanById;
    {
        const xml::Node vroot = xml::Parse(xmlSrc);
        std::vector<const xml::Node*> stk{ &vroot };
        while (!stk.empty()) {
            const xml::Node* nd = stk.back(); stk.pop_back();
            for (const auto& c : nd->children) {
                const std::string nm(c.attr("name"));
                if (c.tag == "hkobject" && nm.size() > 1 && nm[0] == '#') {
                    try { vanById.emplace(static_cast<std::uint32_t>(std::stoul(nm.substr(1))), c); } catch (...) {}
                } else {
                    stk.push_back(&c);
                }
            }
        }
    }

    // 3. Read every mod's patch dir IN LOAD ORDER. Override nodes (#NNNN) accumulate
    //    per id across mods for the bashed merge; new nodes (#mod$N) are namespaced
    //    by their mod prefix, so they never collide.
    struct PatchNode { std::string idStr; xml::Node node; bool isNew; std::string cls; bool skip = false; };
    std::vector<PatchNode> patch;
    std::unordered_map<std::string, std::shared_ptr<IHavokObject>> newById;
    // override #NNNN -> layers in load order (each an <hkobject> full node).
    std::map<std::uint32_t, std::vector<PatchLayer>> ovLayers;
    std::vector<std::uint32_t> ovOrder;  // first-seen order, for stable iteration
    std::error_code ec;
    for (const std::string& patchDir : patchDirs) {
        if (!fs::is_directory(patchDir)) { r.warnings.push_back("patch dir not found (skipped): " + patchDir); continue; }
        for (fs::directory_iterator it(patchDir, ec), end; !ec && it != end; it.increment(ec)) {
            if (!it->is_regular_file()) continue;
            const std::string fname = it->path().filename().string();
            if (fname.empty() || fname[0] != '#' || it->path().extension() != ".txt") continue;
            const std::string fileId = fname.substr(1, fname.size() - 5);  // strip '#' and '.txt'
            auto raw = readFile(it->path().string());
            const std::string rawSrc(raw.begin(), raw.end());
            std::string src = rawSrc;
            xml::StripPatchOriginals(src);           // apply Nemesis MOD_CODE edits (OPEN values)
            xml::Node nd = xml::Parse(src);
            if (nd.tag != "hkobject") { r.warnings.push_back("skipped non-hkobject patch file: " + fname); continue; }
            // The authoritative target id is the object's OWN name (`<hkobject name="#N">`),
            // which is NOT always the filename: BFCO ships #3018.txt whose object is #3108.
            // Keying off the filename mis-targets it (wrong class -> "source drift" skip,
            // silently dropping a clip-trigger array). Prefer the inner name; fall back to
            // the filename if it is missing/malformed.
            std::string innerName(nd.attr("name"));
            const std::string idStr = (innerName.size() > 1 && innerName[0] == '#')
                                      ? innerName.substr(1) : fileId;
            if (idStr.find('$') != std::string::npos) {   // new node — namespaced, add directly
                patch.push_back({ idStr, std::move(nd), true, std::string() });
                patch.back().cls = std::string(patch.back().node.attr("class"));
            } else {                                       // override — collect layers by #NNNN
                std::uint32_t id = 0;
                try { id = static_cast<std::uint32_t>(std::stoul(idStr, nullptr, 10)); } catch (...) { continue; }
                if (ovLayers.find(id) == ovLayers.end()) ovOrder.push_back(id);
                ovLayers[id].push_back({ std::move(nd), changedFields(rawSrc) });
            }
        }
    }
    // Bashed-merge each override's layers against the vanilla node -> one PatchNode.
    for (std::uint32_t id : ovOrder) {
        const auto& layers = ovLayers[id];
        std::vector<const PatchLayer*> ptrs; ptrs.reserve(layers.size());
        for (const auto& l : layers) ptrs.push_back(&l);
        const auto vit = vanById.find(id);
        xml::Node merged = (vit != vanById.end()) ? bashMerge(vit->second, ptrs) : layers.back().node;
        if (layers.size() > 1) ++r.mergedConflicts;
        char buf[16]; std::snprintf(buf, sizeof buf, "%04u", id);
        patch.push_back({ buf, std::move(merged), false, std::string(layers.front().node.attr("class")) });
    }

    // 4. Pass A — bind each patch id to an object (override: existing; new: created).
    for (auto& pn : patch) {
        std::shared_ptr<IHavokObject> obj;
        if (pn.isNew) {
            obj = HavokRegistry::Create(pn.cls);
            if (!obj) { if (std::find(r.unsupportedClasses.begin(), r.unsupportedClasses.end(), pn.cls) == r.unsupportedClasses.end()) r.unsupportedClasses.push_back(pn.cls); continue; }
            newById[pn.idStr] = obj;
            ++r.added;
        } else {
            const std::uint32_t id = static_cast<std::uint32_t>(asLong(pn.idStr));
            auto vit = byNum.find(id);
            if (vit == byNum.end()) { pn.skip = true; r.warnings.push_back("override target #" + pn.idStr + " not found in vanilla"); continue; }
            if (vit->second->ClassName() != pn.cls) {
                // Aligned to a different class than the patch expects — almost always the
                // vanilla binary and its tagfile came from different sources (spec §14).
                // Skip rather than corrupt the wrong-class object.
                pn.skip = true;
                ++r.skippedMismatch;
                r.warnings.push_back("skipped #" + pn.idStr + " (source drift): vanilla=" +
                                     vit->second->ClassName() + " patch=" + pn.cls);
                continue;
            }
            ++r.overrides;
        }
    }

    // Resolver: numeric -> byNum (reflects in-place overrides); symbolic -> newById.
    Ctx ctx;
    ctx.r = &r;
    ctx.ref = [&](const std::string& idNoHash) -> std::shared_ptr<IHavokObject> {
        if (idNoHash.find('$') != std::string::npos) {
            auto it = newById.find(idNoHash);
            if (it != newById.end()) return it->second;
        } else {
            auto it = byNum.find(static_cast<std::uint32_t>(asLong(idNoHash)));
            if (it != byNum.end()) return it->second;
        }
        ++r.refsUnresolved;
        return nullptr;
    };

    // 5. Symbol tables. First populate the three symbol nodes (for their pointers and
    //    non-symbol fields), then rebuild their parallel arrays as ALIGNED tuples so
    //    name/type/value can never desync. Finally read the corrected name tables into
    //    the resolver so transitions/bindings resolve $eventID/$variableID correctly.
    auto isSymbolCls = [](const std::string& c) {
        return c == "hkbBehaviorGraphStringData" || c == "hkbBehaviorGraphData" || c == "hkbVariableValueSet";
    };
    for (auto& pn : patch)
        if (!pn.isNew && !pn.skip && isSymbolCls(pn.cls))
            if (auto o = byNum.find(static_cast<std::uint32_t>(asLong(pn.idStr))); o != byNum.end())
                populate(o->second, pn.node, ctx);

    if (sdId && gdId && vsId) {
        auto sd = std::dynamic_pointer_cast<hkbBehaviorGraphStringData>(byNum[sdId]);
        auto gd = std::dynamic_pointer_cast<hkbBehaviorGraphData>(byNum[gdId]);
        auto vs = std::dynamic_pointer_cast<hkbVariableValueSet>(byNum[vsId]);
        if (sd && gd && vs)
            mergeSymbols(patchDirs, sdId, gdId, vsId,
                         vanById.count(sdId) ? &vanById[sdId] : nullptr,
                         vanById.count(gdId) ? &vanById[gdId] : nullptr,
                         vanById.count(vsId) ? &vanById[vsId] : nullptr,
                         sd.get(), gd.get(), vs.get());
    }

    // Resolver indices from the corrected string data (the single graph stringData).
    for (const auto& [off, obj] : des.DeserializedObjects())
        if (auto sd = std::dynamic_pointer_cast<hkbBehaviorGraphStringData>(obj)) {
            for (std::size_t i = 0; i < sd->m_eventNames.size(); ++i)    ctx.evtIdx[sd->m_eventNames[i]] = static_cast<int>(i);
            for (std::size_t i = 0; i < sd->m_variableNames.size(); ++i) ctx.varIdx[sd->m_variableNames[i]] = static_cast<int>(i);
            break;
        }

    // 6. Pass B — populate everything (stringData already done; skip to avoid redo).
    for (auto& pn : patch) {
        if (pn.skip) continue;
        std::shared_ptr<IHavokObject> obj = pn.isNew
            ? newById.count(pn.idStr) ? newById[pn.idStr] : nullptr
            : [&] { auto it = byNum.find(static_cast<std::uint32_t>(asLong(pn.idStr))); return it != byNum.end() ? it->second : nullptr; }();
        if (!obj) continue;
        if (!pn.isNew && isSymbolCls(pn.cls)) continue;  // symbol nodes handled in step 5
        if (!populate(obj, pn.node, ctx))
            if (std::find(r.unsupportedClasses.begin(), r.unsupportedClasses.end(), pn.cls) == r.unsupportedClasses.end())
                r.unsupportedClasses.push_back(pn.cls);
    }

    // 6b. Native emit (instead of serializing a merged binary): either the vanilla BASE
    //     bundle (tagfile ids, no mods) or this mod's per-mod DELTA (overrides as full
    //     nodes + new mod$N nodes + added vocab). Both key nodes by the stable tagfile id
    //     so the runtime merges deltas onto the base. Reuses the decompiler's per-class
    //     YAML emit, so the native shape can never drift from the loader's format.
    if (!nativeDeltaDir.empty() || !vanBaseDir.empty()) {
        std::shared_ptr<hkbBehaviorGraph> bg;
        for (auto& [id, obj] : byNum)
            if (auto g = std::dynamic_pointer_cast<hkbBehaviorGraph>(obj)) { bg = g; break; }
        if (!bg) { r.error = "native emit: no hkbBehaviorGraph in vanilla graph"; return r; }

        // Every vanilla object -> its tagfile #NNNN (string); new objects -> mod$N.
        std::unordered_map<const void*, std::string> stableIds;
        for (const auto& [off, obj] : des.DeserializedObjects())
            if (auto it = oracle.off2id.find(off); it != oracle.off2id.end())
                stableIds[obj.get()] = std::to_string(it->second);

        if (!vanBaseDir.empty()) {   // vanilla base: full decompile with tagfile ids
            const auto dr = DecompileBehaviorTree(bg, vanBaseDir, &stableIds);
            if (!dr.ok) { r.error = "base emit: " + dr.error; return r; }
            r.ok = true; return r;
        }

        for (const auto& [mid, obj] : newById) stableIds[obj.get()] = mid;

        // This mod's nodes: override #NNNN (skip the vocab symbol nodes) + new mod$N.
        std::set<std::string> deltaIds;
        for (const auto& pn : patch) {
            if (pn.skip) continue;
            if (pn.isNew) { deltaIds.insert(pn.idStr); continue; }
            const std::uint32_t id = static_cast<std::uint32_t>(asLong(pn.idStr));
            if (id == sdId || id == gdId || id == vsId) continue;   // vocab -> additive.yaml
            deltaIds.insert(std::to_string(id));
        }

        // Added vocab = merged names - vanilla names (mergeSymbols unions, so the tail is new).
        std::set<std::string> addedEv, addedVar, addedCp;
        if (auto sdLive = std::dynamic_pointer_cast<hkbBehaviorGraphStringData>(
                byNum.count(sdId) ? byNum[sdId] : nullptr);
            sdLive && vanById.count(sdId)) {
            auto diff = [](const std::vector<std::string>& merged, const std::vector<std::string>& van,
                           std::set<std::string>& out) {
                const std::set<std::string> v(van.begin(), van.end());
                for (const auto& nm : merged) if (!v.count(nm)) out.insert(nm);
            };
            diff(sdLive->m_eventNames,            strArray(vanById[sdId], "eventNames"),            addedEv);
            diff(sdLive->m_variableNames,         strArray(vanById[sdId], "variableNames"),         addedVar);
            diff(sdLive->m_characterPropertyNames, strArray(vanById[sdId], "characterPropertyNames"), addedCp);
        }

        const auto dr = DecompileNativeDelta(bg, stableIds, deltaIds, addedEv, addedVar, addedCp,
                                             nativeDeltaDir, &r.warnings);
        if (!dr.ok) { r.error = "delta emit: " + dr.error; return r; }
        r.ok = true; return r;
    }

    // 7. Serialize the merged graph from root.
    try {
        PackFileSerializer ser;
        BinaryWriterEx bw;
        ser.Serialize(root, bw, des._header);
        const auto out = bw.Data();
        fs::create_directories(fs::path(outBin).parent_path(), ec);
        std::ofstream of(outBin, std::ios::binary | std::ios::trunc);
        of.write(reinterpret_cast<const char*>(out.data()), static_cast<std::streamsize>(out.size()));
        if (!of) { r.error = "failed to write output: " + outBin; return r; }
    } catch (const std::exception& e) {
        r.error = std::string("serialize: ") + e.what();
        return r;
    }
    r.ok = true;
    return r;
}

}  // namespace havok::sct
