// BehaviorBuilder (Tier B) implementation. Retargets HKBuild's BehaviorXmlEmitter
// to construct havok-core Tier-A objects directly. Pure C++ — no ryml.

#include "havok/model/BehaviorBuilder.h"
#include "havok/model/HavokEnums.h"
#include "havok/cross/Cross.h"          // the cross-kind membrane (bone name<->index; more folding in)

#include <array>
#include <cstdlib>
#include <cassert>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace havok::model {

namespace {

// Sanity cap on a *computed* bone-weight array length (named/preset path only).
// Real Skyrim skeletons top out in the low hundreds of bones (even heavily-modded
// XPMSE rigs stay well under a thousand); a length beyond this indicates a bad
// bone_count override or corrupt data, so we refuse rather than allocate a
// garbage-length hkbBoneWeightArray. The raw path never uses this — it emits
// exactly the authored float list.
constexpr int kMaxBoneWeightArrayLength = 8192;

// Parse a float-string the way the C# emitter's downstream compiler did. The Def
// keeps the authored text (e.g. "1.000000"); we parse it to float here. Invalid /
// empty -> 0.f (defensive; authored values are always well-formed floats).
float pf(const std::string& s) {
    if (s.empty()) return 0.f;
    try { return std::stof(s); } catch (...) { return 0.f; }
}

// Parse "(x y z w)" (or bare "x y z w") into a Vector4. Used for axisOfRotation,
// quad variable values, etc. — mirrors the vanilla XML vector token format.
// hkVector4 text -> Vector4, via the shared membrane parser (havok::cross::parseVec4) — the ONE
// vec4 text codec, also used by havok-model parseVec4Raw. (B4 collapsed: no more divergent twin.)
Vector4 pv4(const std::string& raw) {
    const auto q = havok::cross::parseVec4(raw);
    return Vector4{ q[0], q[1], q[2], q[3] };
}

// Same token format as pv4, but produces a Quaternion (distinct value type used by
// rotation fields such as worldFromModelRotation / alignWithGroundRotation).
Quaternion pq4(const std::string& raw) {
    Vector4 v = pv4(raw);
    return Quaternion{v.x, v.y, v.z, v.w};
}

template <typename Map>
long en(const std::string& v, const Map& table) {
    return enums::ResolveEnum(v, table);
}

} // namespace

// ── Name indexes ──────────────────────────────────────────────────────────────
// Stage 4b: name->index resolution has moved to ResolveBehaviorBindings (run before the
// builder). These accessors therefore hold NO name map and do NO lookup — they return the
// pre-resolved index. `name` must be empty by the time the builder runs (the pass cleared
// every named ref); assert it in debug so a future uncovered site is caught, not silently
// mis-emitted. The variable accessor keeps the numeric-index OOB guard.
int BehaviorBuilder::resolveEventId(const std::optional<std::string>& name, int fallback) const {
    assert(!name && "event name not pre-resolved — a ResolveBehaviorBindings site is missing");
    (void)name;
    // A NUMERIC event id must be in range (mirror resolveVariableIndex, BR-16/BR-31). -1 = "no event"
    // is engine-valid; anything else outside [0,count) indexes an event roster OOB at runtime. Refuse
    // loudly so the graph fails compile (BR falls back + logs) instead of baking a bad id.
    if (fallback < -1 || fallback >= _eventCount)
        throw std::runtime_error("BehaviorBuilder: numeric event id " + std::to_string(fallback) +
            " out of range [0," + std::to_string(_eventCount) + ") — refusing to emit an OOB event id (BR-31).");
    return fallback;
}

int BehaviorBuilder::resolveVariableIndex(const std::optional<std::string>& name, int fallback) const {
    assert(!name && "variable name not pre-resolved — a ResolveBehaviorBindings site is missing");
    (void)name;
    // A NUMERIC variable index MUST be in range. The engine indexes wordVariableValues[idx] at
    // char-setup — the synchronized-clip "iState_" pass sizes a BSTArray by wordVariableValues[idx],
    // so an OOB idx reads adjacent heap -> a garbage size -> `rep stosq` overrun: the recurring
    // SkyrimSE.exe+0xBC2AE0 char-setup crash (docs BR-16). -1 = "no binding" is engine-valid (vanilla
    // sync clips use it) and passes; anything else out of [0,count) is refused so THIS graph fails
    // compile (BR falls back + logs the offending index) instead of crashing char-setup.
    if (fallback < -1 || fallback >= _variableCount)
        throw std::runtime_error("BehaviorBuilder: numeric variable index " + std::to_string(fallback) +
            " out of range [0," + std::to_string(_variableCount) + ") — an OOB variable index crashes char-setup "
            "(rep stosq, BR-16). Refusing to emit it.");
    return fallback;
}

int BehaviorBuilder::resolveCharPropIndex(const std::optional<std::string>& name, int fallback) const {
    assert(!name && "charprop name not pre-resolved — a ResolveBehaviorBindings site is missing");
    (void)name;
    // A NUMERIC character-property index must be in range (mirror resolveVariableIndex, BR-31). -1 =
    // "no binding" is engine-valid; anything else outside [0,count) reads the character-property table
    // OOB. Refuse loudly rather than bake a bad index.
    if (fallback < -1 || fallback >= _charPropCount)
        throw std::runtime_error("BehaviorBuilder: numeric character-property index " + std::to_string(fallback) +
            " out of range [0," + std::to_string(_charPropCount) + ") — refusing to emit an OOB char-property index (BR-31).");
    return fallback;
}

std::shared_ptr<hkbStringEventPayload> BehaviorBuilder::resolvePayload(const std::string& payload) {
    if (payload.empty() || payload == "null") return nullptr;
    auto it = _payloadMemo.find(payload);
    if (it != _payloadMemo.end()) return it->second;
    auto p = std::make_shared<hkbStringEventPayload>();
    p->m_data = payload;
    _payloadMemo[payload] = p;
    return p;
}

void BehaviorBuilder::fillEventBase(hkbEventBase& ev, const InlineEventDef& def) {
    ev.m_id = resolveEventId(def.event, def.id);
    if (def.payload) ev.m_payload = resolvePayload(*def.payload);
}

void BehaviorBuilder::fillEventBase(hkbEventBase& ev, const std::optional<std::string>& name,
                                    int id, const std::string& payload) {
    ev.m_id = resolveEventId(name, id);
    ev.m_payload = resolvePayload(payload);
}

// ── Bindings ──────────────────────────────────────────────────────────────────
std::shared_ptr<hkbVariableBindingSet>
BehaviorBuilder::buildBindingSet(const std::vector<BindingDef>& bindings) {
    if (bindings.empty()) return nullptr;
    auto set = std::make_shared<hkbVariableBindingSet>();
    int enableIndex = -1;
    for (int i = 0; i < static_cast<int>(bindings.size()); ++i) {
        const auto& b = bindings[i];
        hkbVariableBindingSetBinding out;
        out.m_memberPath = b.memberPath;
        long bt = en(b.bindingType, enums::BindingType());
        out.m_bindingType = static_cast<std::int8_t>(bt);
        out.m_variableIndex = (bt == 1 /*CHARACTER_PROPERTY*/)
            ? resolveCharPropIndex(b.variable, b.variableIndex)
            : resolveVariableIndex(b.variable, b.variableIndex);
        out.m_bitIndex = static_cast<std::int8_t>(b.bitIndex);
        if (b.enableTarget) enableIndex = i;
        set->m_bindings.push_back(out);
    }
    set->m_indexOfBindingToEnable = enableIndex;
    return set;
}

// ── Bone weights ──────────────────────────────────────────────────────────────
//
// A bone-weight source may be authored three ways:
//   • raw    — an explicit `count` + space-separated `values`. THIS IS THE PRIMARY
//              PATH: every bone-weight source in all shipped/vanilla data is raw and
//              it is the only path exercised. Its output must stay byte-identical, so
//              the raw branch below is deliberately left untouched.
//   • named  — a { boneName: weight } map, expanded here into a flat array indexed by
//              the skeleton's bone order (bones not listed default to 0.0).
//   • preset — a named preset from bone_presets.yaml, expanded to `named` by the
//              loader (YamlBehaviorLoader::expandPreset) before we ever see it.
//
// OPTIONAL / CURRENTLY-UNUSED: no shipped data uses named/preset today — there is no
// bone_presets.yaml anywhere and every real source is raw — so the named branch below
// is DORMANT. It is kept as a documented, hardened nice-to-have.
//
// INVARIANT ENFORCED (named/preset path): it is now IMPOSSIBLE to emit a wrong-length
// or out-of-bounds hkbBoneWeightArray. Specifically —
//   (1) a non-empty skeleton (boneNames) is required, mirroring CharacterBuilder;
//   (2) the array length (bone_count override, else boneNames.size()) must be in
//       [1, kMaxBoneWeightArrayLength] — otherwise we throw, never allocate garbage;
//   (3) every named bone must resolve to an in-range index, else we throw.
// Any violation fails LOUDLY with std::runtime_error rather than silently producing a
// malformed array that would crash / read OOB in the game runtime.
std::shared_ptr<hkbBoneWeightArray>
BehaviorBuilder::buildBoneWeights(const BoneWeightsDef& bw) {
    auto arr = std::make_shared<hkbBoneWeightArray>();
    int count = bw.count;
    std::string values = bw.values;

    if (bw.IsNamed()) {
        // (1) A named/preset mask is meaningless without a skeleton to index into.
        if (_data.boneNames.empty())
            throw std::runtime_error(
                "BehaviorBuilder::buildBoneWeights: named/preset bone-weight mask but no "
                "skeleton bone names are loaded (skeleton.yaml missing) — refusing to emit "
                "a bone-weight array of unknown length.");

        // (2) Determine and validate the emitted array length. A bone_count override
        // must be sane; without one we use the skeleton's bone count.
        const int skelCount   = static_cast<int>(_data.boneNames.size());
        const int outputCount = bw.boneCount.value_or(skelCount);
        if (outputCount <= 0 || outputCount > kMaxBoneWeightArrayLength)
            throw std::runtime_error(
                "BehaviorBuilder::buildBoneWeights: computed bone-weight array length " +
                std::to_string(outputCount) + " is out of range (must be 1.." +
                std::to_string(kMaxBoneWeightArrayLength) + ") — bad bone_count or skeleton.");

        std::vector<float> weights(static_cast<std::size_t>(outputCount), 0.f);
        if (bw.named) {
            for (const auto& [boneName, weightStr] : *bw.named) {
                const int idx = havok::cross::boneIndexByName(boneName, _data.boneNames);   // membrane
                // (3) Every named bone must land inside the array. An unknown bone
                // (wrong/mismatched skeleton) or an index past the array end is a hard
                // error, never a silent skip — that is exactly the OOB landmine.
                if (idx < 0)
                    throw std::runtime_error(
                        "BehaviorBuilder::buildBoneWeights: named bone '" + boneName +
                        "' not found in skeleton — wrong or mismatched skeleton.");
                if (idx >= outputCount)
                    throw std::runtime_error(
                        "BehaviorBuilder::buildBoneWeights: bone '" + boneName + "' index " +
                        std::to_string(idx) + " exceeds bone-weight array length " +
                        std::to_string(outputCount) + ".");
                weights[static_cast<std::size_t>(idx)] = pf(weightStr);
            }
        }
        arr->m_boneWeights = std::move(weights);
        return arr;
    }

    // Raw format: space-separated floats. PRIMARY PATH — unchanged, byte-identical.
    if (count > 0 && !values.empty()) {
        std::stringstream ss(values);
        float f;
        while (ss >> f) arr->m_boneWeights.push_back(f);
    }
    return arr;
}

// hkbBoneIndexArray: resolve the array's bone NAMES to indices against the
// skeleton bone list (data.boneNames), mirroring the bone-weight resolver.
// Referenced by ragdoll/keyframe modifiers via naming convention (<modifier>_bones
// / <modifier>_keyframedBonesList); returns null if the named array is absent.
std::shared_ptr<hkbBoneIndexArray>
BehaviorBuilder::buildBoneIndexArray(const std::string& name) {
    auto it = _data.boneIndexArrays.find(name);
    if (it == _data.boneIndexArrays.end()) return nullptr;
    auto arr = std::make_shared<hkbBoneIndexArray>();
    // Raw indices (our decompiled source) are used directly; bone names (vanilla
    // source) are resolved against the skeleton bone list.
    if (!it->second.boneIndices.empty()) {
        for (int v : it->second.boneIndices) arr->m_boneIndices.push_back(static_cast<std::int16_t>(v));
        return arr;
    }
    // A bone NAME must resolve to a real skeleton index. Emitting -1 into an ITERATED
    // bone array is fatal: the engine walks the array at char-setup and uses -1 (=0xFFFF
    // unsigned) as an index, sizing a downstream array to garbage → the `rep stosq`
    // overrun crash (BR-10). So refuse — mirror buildBoneWeights/resolveEventId and throw,
    // which fails THIS graph's compile (BR falls back) instead of shipping a crasher.
    if (_data.boneNames.empty())
        throw std::runtime_error("BehaviorBuilder: hkbBoneIndexArray '" + name +
            "' resolves bones by NAME but the loaded skeleton bone list is EMPTY "
            "(skeleton.hkx not read/resolved) — refusing to emit a -1-filled array.");
    arr->m_boneIndices.reserve(it->second.boneNames.size());
    for (const auto& bn : it->second.boneNames) {
        const int idx = havok::cross::boneIndexByName(bn, _data.boneNames);   // membrane: name -> index
        if (idx < 0)
            throw std::runtime_error("BehaviorBuilder: hkbBoneIndexArray '" + name +
                "' bone '" + bn + "' is not in the loaded skeleton (name mismatch) — a -1 "
                "bone index crashes char-setup (BR-10).");
        arr->m_boneIndices.push_back(static_cast<std::int16_t>(idx));
    }
    return arr;
}

// ── Clip triggers ─────────────────────────────────────────────────────────────
std::shared_ptr<hkbClipTriggerArray>
BehaviorBuilder::buildTriggers(const std::vector<ClipTriggerDef>& triggers) {
    if (triggers.empty()) return nullptr;
    auto arr = std::make_shared<hkbClipTriggerArray>();
    for (const auto& t : triggers) {
        hkbClipTrigger out;
        out.m_localTime = pf(t.localTime);
        out.m_event.m_id = resolveEventId(t.event, t.eventId);
        out.m_event.m_payload = resolvePayload(t.payload);
        out.m_relativeToEndOfClip = t.relativeToEndOfClip;
        out.m_acyclic             = t.acyclic;
        out.m_isAnnotation        = t.isAnnotation;
        arr->m_triggers.push_back(std::move(out));
    }
    return arr;
}

// ── Event property arrays (enter/exit notify) ─────────────────────────────────
std::shared_ptr<hkbStateMachineEventPropertyArray>
BehaviorBuilder::buildEventArray(const std::vector<EventPropertyDef>& events) {
    if (events.empty()) return nullptr;
    auto arr = std::make_shared<hkbStateMachineEventPropertyArray>();
    for (const auto& e : events) {
        hkbEventProperty ev;
        ev.m_id = resolveEventId(e.event, e.id);
        ev.m_payload = resolvePayload(e.payload);
        arr->m_events.push_back(std::move(ev));
    }
    return arr;
}

// ── Transition effects ────────────────────────────────────────────────────────
std::shared_ptr<hkbTransitionEffect>
BehaviorBuilder::buildTransitionEffect(const std::string& name) {
    if (name.empty() || name == "null") return nullptr;
    auto it = _effectMemo.find(name);
    if (it != _effectMemo.end()) return it->second;

    auto dit = _data.transitionEffects.find(name);
    if (dit == _data.transitionEffects.end()) return nullptr;
    const TransitionEffectDef& def = dit->second;

    auto eff = std::make_shared<hkbBlendingTransitionEffect>();
    eff->m_name     = def.name;
    eff->m_userData = static_cast<std::uint64_t>(def.userData);
    eff->m_selfTransitionMode =
        static_cast<std::int8_t>(en(def.selfTransitionMode, enums::SelfTransitionMode()));
    eff->m_eventMode = static_cast<std::int8_t>(en(def.eventMode, enums::EventMode()));
    eff->m_duration  = pf(def.duration);
    eff->m_toGeneratorStartTimeFraction = pf(def.toGeneratorStartTimeFraction);
    eff->m_flags     = static_cast<std::uint16_t>(en(def.flags, enums::FlagBits()));
    eff->m_endMode   = static_cast<std::int8_t>(en(def.endMode, enums::EndMode()));
    eff->m_blendCurve = static_cast<std::int8_t>(en(def.blendCurve, enums::BlendCurve()));
    eff->m_applySelfTransition     = def.applySelfTransition;
    eff->m_initializeCharacterPose = def.initializeCharacterPose;
    if (def.bindings) eff->m_variableBindingSet = buildBindingSet(*def.bindings);

    _effectMemo[name] = eff;
    return eff;
}

// ── Transition info array ─────────────────────────────────────────────────────
std::shared_ptr<hkbStateMachineTransitionInfoArray>
BehaviorBuilder::buildTransitions(const std::vector<TransitionInfoDef>& transitions) {
    if (transitions.empty()) return nullptr;
    auto arr = std::make_shared<hkbStateMachineTransitionInfoArray>();
    for (const auto& t : transitions) {
        hkbStateMachineTransitionInfo out;
        out.m_triggerInterval.m_enterEventId = resolveEventId(t.triggerInterval.enterEvent, t.triggerInterval.enterEventId);
        out.m_triggerInterval.m_exitEventId  = resolveEventId(t.triggerInterval.exitEvent,  t.triggerInterval.exitEventId);
        out.m_triggerInterval.m_enterTime    = pf(t.triggerInterval.enterTime);
        out.m_triggerInterval.m_exitTime     = pf(t.triggerInterval.exitTime);
        out.m_initiateInterval.m_enterEventId = resolveEventId(t.initiateInterval.enterEvent, t.initiateInterval.enterEventId);
        out.m_initiateInterval.m_exitEventId  = resolveEventId(t.initiateInterval.exitEvent,  t.initiateInterval.exitEventId);
        out.m_initiateInterval.m_enterTime    = pf(t.initiateInterval.enterTime);
        out.m_initiateInterval.m_exitTime     = pf(t.initiateInterval.exitTime);
        out.m_transition = buildTransitionEffect(t.transition);
        // Transition condition: an hkbExpressionCondition from the expression
        // string (`condition: "var == 1"`). Absent → null.
        if (t.condition && *t.condition != "null") {
            auto& cached = _conditionMemo[*t.condition];
            if (!cached) { cached = std::make_shared<hkbExpressionCondition>(); cached->m_expression = *t.condition; }
            out.m_condition = cached;
        } else if (t.conditionString && *t.conditionString != "null") {
            auto sc = std::make_shared<hkbStringCondition>();
            sc->m_conditionString = *t.conditionString;
            out.m_condition = sc;
        }
        out.m_eventId           = resolveEventId(t.event, t.eventId);
        out.m_toStateId         = t.toStateId;
        out.m_fromNestedStateId = t.fromNestedStateId;
        out.m_toNestedStateId   = t.toNestedStateId;
        out.m_priority          = static_cast<std::int16_t>(t.priority);
        out.m_flags             = static_cast<std::int16_t>(en(t.flags, enums::TransitionFlags()));
        arr->m_transitions.push_back(std::move(out));
    }
    return arr;
}

// ── Graph data ────────────────────────────────────────────────────────────────
std::shared_ptr<hkbBehaviorGraphData> BehaviorBuilder::buildGraphData() {
    if (!_data.graphData) return nullptr;
    const auto& gd = *_data.graphData;

    auto strData = std::make_shared<hkbBehaviorGraphStringData>();
    for (const auto& e : gd.events)    strData->m_eventNames.push_back(e.name);
    for (const auto& v : gd.variables) strData->m_variableNames.push_back(v.name);
    for (const auto& cp : gd.characterPropertyNames)
        strData->m_characterPropertyNames.push_back(cp.name);

    auto data = std::make_shared<hkbBehaviorGraphData>();

    // variableInfos
    for (const auto& v : gd.variables) {
        hkbVariableInfo info;
        info.m_role.m_role  = static_cast<std::int16_t>(en(v.role, enums::Role()));
        info.m_role.m_flags = static_cast<std::int16_t>(v.roleFlags);
        info.m_type = static_cast<std::int8_t>(en(v.type, enums::VariableType()));
        data->m_variableInfos.push_back(info);
    }

    // characterPropertyInfos
    int charPropCount = !gd.characterPropertyNames.empty()
        ? static_cast<int>(gd.characterPropertyNames.size())
        : gd.characterPropertyInfoCount;
    for (int i = 0; i < charPropCount; ++i) {
        hkbVariableInfo info;
        info.m_role.m_role = 0;  // ROLE_DEFAULT
        if (i < static_cast<int>(gd.characterPropertyNames.size())) {
            info.m_role.m_flags = static_cast<std::int16_t>(
                en(gd.characterPropertyNames[i].flags, enums::RoleFlags()));
            info.m_type = static_cast<std::int8_t>(
                en(gd.characterPropertyNames[i].type, enums::VariableType()));
        } else {
            info.m_type = static_cast<std::int8_t>(en("VARIABLE_TYPE_POINTER", enums::VariableType()));
        }
        data->m_characterPropertyInfos.push_back(info);
    }

    // eventInfos
    for (const auto& e : gd.events) {
        hkbEventInfo info;
        info.m_flags = static_cast<std::uint32_t>(en(e.flags, enums::EventInfoFlags()));
        data->m_eventInfos.push_back(info);
    }

    // variableInitialValues (hkbVariableValueSet)
    auto vvs = std::make_shared<hkbVariableValueSet>();
    for (const auto& v : gd.variables) {
        hkbVariableValue val;
        val.m_value = v.value;
        vvs->m_wordVariableValues.push_back(val);
    }
    // quad variable values
    if (!gd.quadVariableValues.empty()) {
        for (const auto& qv : gd.quadVariableValues)
            vvs->m_quadVariableValues.push_back(pv4(qv));
    } else {
        for (const auto& v : gd.variables) {
            if (v.type == "VARIABLE_TYPE_VECTOR4" || v.type == "VARIABLE_TYPE_QUATERNION" ||
                v.type == "VARIABLE_TYPE_VECTOR3") {
                if (v.quadValue) vvs->m_quadVariableValues.push_back(pv4(*v.quadValue));
                else if (v.type == "VARIABLE_TYPE_QUATERNION")
                    vvs->m_quadVariableValues.push_back(Vector4{0.f, 0.f, 0.f, 1.f});
                else
                    vvs->m_quadVariableValues.push_back(Vector4{0.f, 0.f, 0.f, 0.f});
            }
        }
    }

    data->m_variableInitialValues = vvs;
    data->m_stringData = strData;
    return data;
}

// ── Per-kind generator builders ───────────────────────────────────────────────
std::shared_ptr<hkbClipGenerator> BehaviorBuilder::buildClip(const ClipGeneratorDef& def) {
    auto clip = std::make_shared<hkbClipGenerator>();
    clip->m_userData      = static_cast<std::uint64_t>(def.userData);
    clip->m_name          = def.name;
    clip->m_animationName = def.animationName;
    clip->m_cropStartAmountLocalTime   = pf(def.cropStartAmountLocalTime);
    clip->m_cropEndAmountLocalTime     = pf(def.cropEndAmountLocalTime);
    clip->m_startTime                  = pf(def.startTime);
    clip->m_playbackSpeed              = pf(def.playbackSpeed);
    clip->m_enforcedDuration           = pf(def.enforcedDuration);
    clip->m_userControlledTimeFraction = pf(def.userControlledTimeFraction);
    clip->m_animationBindingIndex      = static_cast<std::int16_t>(def.animationBindingIndex);
    clip->m_mode  = static_cast<std::int8_t>(en(def.mode, enums::PlaybackMode()));
    clip->m_flags = static_cast<std::int8_t>(def.flags);
    if (def.bindings) clip->m_variableBindingSet = buildBindingSet(*def.bindings);
    if (def.triggers) clip->m_triggers = buildTriggers(*def.triggers);
    return clip;
}

std::shared_ptr<hkbBlenderGenerator> BehaviorBuilder::buildBlender(const BlenderGeneratorDef& def) {
    auto blend = std::make_shared<hkbBlenderGenerator>();
    blend->m_userData = static_cast<std::uint64_t>(def.userData);
    blend->m_name     = def.name;
    blend->m_referencePoseWeightThreshold = pf(def.referencePoseWeightThreshold);
    blend->m_blendParameter               = pf(def.blendParameter);
    blend->m_minCyclicBlendParameter      = pf(def.minCyclicBlendParameter);
    blend->m_maxCyclicBlendParameter      = pf(def.maxCyclicBlendParameter);
    blend->m_indexOfSyncMasterChild       = static_cast<std::int16_t>(def.indexOfSyncMasterChild);
    blend->m_flags             = static_cast<std::int16_t>(def.flags);
    blend->m_subtractLastChild = def.subtractLastChild;
    if (def.bindings) blend->m_variableBindingSet = buildBindingSet(*def.bindings);

    for (const auto& c : def.children) {
        if (c.generator.empty() || c.generator == "null") continue;  // mirror emitter
        auto child = std::make_shared<hkbBlenderGeneratorChild>();
        child->m_generator = std::dynamic_pointer_cast<hkbGenerator>(buildNode(c.generator));
        // Build whenever a boneWeights block is present — the decompiler emits it
        // iff the original pointer was non-null, so a present-but-empty (count 0)
        // block must round-trip to a non-null empty array, not null. (HasData()
        // gated out the empty case, silently dropping vanilla pose-matcher arrays.)
        if (c.boneWeights)
            child->m_boneWeights = buildBoneWeights(*c.boneWeights);
        child->m_weight               = pf(c.weight);
        child->m_worldFromModelWeight = pf(c.worldFromModelWeight);
        if (c.bindings) child->m_variableBindingSet = buildBindingSet(*c.bindings);
        blend->m_children.push_back(child);
    }
    return blend;
}

std::shared_ptr<hkbManualSelectorGenerator>
BehaviorBuilder::buildSelector(const ManualSelectorDef& def) {
    auto sel = std::make_shared<hkbManualSelectorGenerator>();
    sel->m_userData = static_cast<std::uint64_t>(def.userData);
    sel->m_name     = def.name;
    for (const auto& g : def.generators)
        sel->m_generators.push_back(std::dynamic_pointer_cast<hkbGenerator>(buildNode(g)));
    sel->m_selectedGeneratorIndex = static_cast<std::int8_t>(def.selectedGeneratorIndex);
    sel->m_currentGeneratorIndex  = static_cast<std::int8_t>(def.currentGeneratorIndex);
    if (def.bindings) sel->m_variableBindingSet = buildBindingSet(*def.bindings);
    return sel;
}

std::shared_ptr<hkbStateMachineStateInfo> BehaviorBuilder::buildState(const std::string& id, const StateDef& def) {
    // A state builds once and is shared across every SM that lists it, keyed by ID —
    // NOT name. Names are non-unique labels now; a name key would re-collapse shared
    // states (the state-split iceskating bug). Pandora's cross-SM sharing survives.
    if (!id.empty()) {
        if (auto m = _stateMemo.find(id); m != _stateMemo.end()) return m->second;
    }
    auto info = std::make_shared<hkbStateMachineStateInfo>();
    info->m_name        = def.name;
    info->m_stateId     = def.stateId;
    info->m_probability = pf(def.probability);
    info->m_enable      = def.enable;
    info->m_generator   = std::dynamic_pointer_cast<hkbGenerator>(buildNode(def.generator));
    if (def.enterNotifyEvents) info->m_enterNotifyEvents = buildEventArray(*def.enterNotifyEvents);
    if (def.exitNotifyEvents)  info->m_exitNotifyEvents  = buildEventArray(*def.exitNotifyEvents);
    if (def.parsedTransitions) info->m_transitions       = buildTransitions(*def.parsedTransitions);
    if (def.bindings)          info->m_variableBindingSet = buildBindingSet(*def.bindings);
    if (!id.empty()) _stateMemo[id] = info;
    return info;
}

std::shared_ptr<hkbStateMachine> BehaviorBuilder::buildStateMachine(const StateMachineDef& def) {
    auto sm = std::make_shared<hkbStateMachine>();
    sm->m_userData = static_cast<std::uint64_t>(def.userData);
    sm->m_name     = def.name;
    sm->m_eventToSendWhenStateOrTransitionChanges.m_id =
        resolveEventId(def.eventToSendWhenStateOrTransitionChangesEvent, def.eventToSendWhenStateOrTransitionChangesId);
    sm->m_startStateId = def.startStateId;
    sm->m_returnToPreviousStateEventId       = resolveEventId(def.returnToPreviousStateEvent, def.returnToPreviousStateEventId);
    sm->m_randomTransitionEventId            = resolveEventId(def.randomTransitionEvent, def.randomTransitionEventId);
    sm->m_transitionToNextHigherStateEventId = resolveEventId(def.transitionToNextHigherStateEvent, def.transitionToNextHigherStateEventId);
    sm->m_transitionToNextLowerStateEventId  = resolveEventId(def.transitionToNextLowerStateEvent, def.transitionToNextLowerStateEventId);
    sm->m_syncVariableIndex      = resolveVariableIndex(def.syncVariable, def.syncVariableIndex);
    sm->m_wrapAroundStateId      = def.wrapAroundStateId;
    sm->m_maxSimultaneousTransitions = static_cast<std::int8_t>(def.maxSimultaneousTransitions);
    sm->m_startStateMode     = static_cast<std::int8_t>(en(def.startStateMode, enums::StartStateMode()));
    sm->m_selfTransitionMode = static_cast<std::int8_t>(en(def.selfTransitionMode, enums::SmSelfTransitionMode()));
    if (def.bindings) sm->m_variableBindingSet = buildBindingSet(*def.bindings);

    for (const auto& stateName : def.states) {
        auto it = _data.states.find(stateName);
        if (it == _data.states.end())
            throw std::runtime_error("BehaviorBuilder: state machine '" + def.name +
                                     "' references unknown state '" + stateName + "'");
        sm->m_states.push_back(buildState(stateName, it->second));
    }

    if (def.parsedWildcardTransitions && !def.parsedWildcardTransitions->empty())
        sm->m_wildcardTransitions = buildTransitions(*def.parsedWildcardTransitions);

    return sm;
}

std::shared_ptr<BSiStateTaggingGenerator>
BehaviorBuilder::buildStateTagging(const BSiStateTaggingGeneratorDef& def) {
    auto g = std::make_shared<BSiStateTaggingGenerator>();
    g->m_userData = static_cast<std::uint64_t>(def.userData);
    g->m_name     = def.name;
    g->m_pDefaultGenerator = std::dynamic_pointer_cast<hkbGenerator>(buildNode(def.pDefaultGenerator));
    g->m_iStateToSetAs = def.iStateToSetAs;
    g->m_iPriority     = def.iPriority;
    if (def.bindings) g->m_variableBindingSet = buildBindingSet(*def.bindings);
    return g;
}

std::shared_ptr<hkbBehaviorReferenceGenerator>
BehaviorBuilder::buildBehaviorReference(const BehaviorReferenceGeneratorDef& def) {
    auto g = std::make_shared<hkbBehaviorReferenceGenerator>();
    g->m_userData     = static_cast<std::uint64_t>(def.userData);
    g->m_name         = def.name;
    g->m_behaviorName = def.behaviorName;
    if (def.bindings) g->m_variableBindingSet = buildBindingSet(*def.bindings);
    return g;
}

std::shared_ptr<BGSGamebryoSequenceGenerator>
BehaviorBuilder::buildGamebryoSequence(const BGSGamebryoSequenceGeneratorDef& def) {
    auto g = std::make_shared<BGSGamebryoSequenceGenerator>();
    g->m_userData   = static_cast<std::uint64_t>(def.userData);
    g->m_name       = def.name;
    g->m_pSequence  = def.sequence;
    g->m_eBlendModeFunction =
        static_cast<std::int8_t>(en(def.blendModeFunction, enums::BlendModeFunction()));
    g->m_fPercent   = pf(def.percent);
    if (def.bindings) g->m_variableBindingSet = buildBindingSet(*def.bindings);
    return g;
}

// hkbReferencePoseGenerator: a bare generator (reference-pose output). No authored
// fields beyond the generator base; m_skeleton stays null (SERIALIZE_IGNORED).
std::shared_ptr<hkbReferencePoseGenerator>
BehaviorBuilder::buildReferencePose(const ReferencePoseGeneratorDef& def) {
    auto g = std::make_shared<hkbReferencePoseGenerator>();
    g->m_userData = static_cast<std::uint64_t>(def.userData);
    g->m_name     = def.name;
    if (def.bindings) g->m_variableBindingSet = buildBindingSet(*def.bindings);
    return g;
}

// BSIStateManagerModifier: iStateVar + an array of (state machine, StateID,
// iStateToSetAs) tuples. Each pStateMachine resolves through buildNode (memoized,
// so it shares the same object as the generator-tree reference).
std::shared_ptr<BSIStateManagerModifier>
BehaviorBuilder::buildIStateManager(const BSIStateManagerModifierDef& def) {
    auto m = std::make_shared<BSIStateManagerModifier>();
    m->m_userData  = static_cast<std::uint64_t>(def.userData);
    m->m_name      = def.name;
    m->m_enable    = def.enable;
    m->m_iStateVar = resolveVariableIndex(def.iStateVariable, def.iStateVar);
    if (def.bindings) m->m_variableBindingSet = buildBindingSet(*def.bindings);
    for (const auto& sd : def.stateData) {
        BSIStateManagerModifierBSiStateData out;
        out.m_pStateMachine = std::dynamic_pointer_cast<hkbStateMachine>(buildNode(sd.pStateMachine));
        out.m_StateID       = sd.StateID;
        out.m_iStateToSetAs = sd.iStateToSetAs;
        m->m_stateData.push_back(std::move(out));
    }
    return m;
}

// hkbFootIkModifier: gains struct + per-leg ankle-placement data (each leg carries
// an ungroundedEvent). Runtime-state leg members (originalAnkleTransformMS etc.) are
// not modeled — they serialize as zero.
std::shared_ptr<hkbFootIkModifier>
BehaviorBuilder::buildFootIkModifier(const FootIkModifierDef& def) {
    auto m = std::make_shared<hkbFootIkModifier>();
    m->m_userData = static_cast<std::uint64_t>(def.userData);
    m->m_name     = def.name;
    m->m_enable   = def.enable;
    if (def.bindings) m->m_variableBindingSet = buildBindingSet(*def.bindings);

    const auto& g = def.gains; auto& og = m->m_gains;
    og.m_onOffGain = g.onOffGain;                       og.m_groundAscendingGain = g.groundAscendingGain;
    og.m_groundDescendingGain = g.groundDescendingGain; og.m_footPlantedGain = g.footPlantedGain;
    og.m_footRaisedGain = g.footRaisedGain;             og.m_footUnlockGain = g.footUnlockGain;
    og.m_worldFromModelFeedbackGain = g.worldFromModelFeedbackGain; og.m_errorUpDownBias = g.errorUpDownBias;
    og.m_alignWorldFromModelGain = g.alignWorldFromModelGain;       og.m_hipOrientationGain = g.hipOrientationGain;
    og.m_maxKneeAngleDifference = g.maxKneeAngleDifference;         og.m_ankleOrientationGain = g.ankleOrientationGain;

    for (const auto& l : def.legs) {
        hkbFootIkModifierLeg out;
        out.m_prevAnkleRotLS = pq4(l.prevAnkleRotLS);
        out.m_kneeAxisLS     = pv4(l.kneeAxisLS);
        out.m_footEndLS      = pv4(l.footEndLS);
        if (l.ungroundedEvent) fillEventBase(out.m_ungroundedEvent, *l.ungroundedEvent);
        out.m_footPlantedAnkleHeightMS = l.footPlantedAnkleHeightMS;
        out.m_footRaisedAnkleHeightMS  = l.footRaisedAnkleHeightMS;
        out.m_maxAnkleHeightMS = l.maxAnkleHeightMS;
        out.m_minAnkleHeightMS = l.minAnkleHeightMS;
        out.m_maxKneeAngleDegrees = l.maxKneeAngleDegrees;
        out.m_minKneeAngleDegrees = l.minKneeAngleDegrees;
        out.m_verticalError = l.verticalError;
        out.m_maxAnkleAngleDegrees = l.maxAnkleAngleDegrees;
        out.m_hipIndex   = static_cast<std::int16_t>(l.hipIndex);
        out.m_kneeIndex  = static_cast<std::int16_t>(l.kneeIndex);
        out.m_ankleIndex = static_cast<std::int16_t>(l.ankleIndex);
        out.m_hitSomething = l.hitSomething;
        out.m_isPlantedMS  = l.isPlantedMS;
        out.m_isOriginalAnkleTransformMSSet = l.isOriginalAnkleTransformMSSet;
        m->m_legs.push_back(std::move(out));
    }

    m->m_raycastDistanceUp      = def.raycastDistanceUp;
    m->m_raycastDistanceDown    = def.raycastDistanceDown;
    m->m_originalGroundHeightMS = def.originalGroundHeightMS;
    m->m_errorOut               = def.errorOut;
    m->m_errorOutTranslation     = pv4(def.errorOutTranslation);
    m->m_alignWithGroundRotation = pq4(def.alignWithGroundRotation);
    m->m_verticalOffset       = def.verticalOffset;
    m->m_collisionFilterInfo  = def.collisionFilterInfo;
    m->m_forwardAlignFraction = def.forwardAlignFraction;
    m->m_sidewaysAlignFraction = def.sidewaysAlignFraction;
    m->m_sidewaysSampleWidth  = def.sidewaysSampleWidth;
    m->m_useTrackData         = def.useTrackData;
    m->m_lockFeetWhenPlanted  = def.lockFeetWhenPlanted;
    m->m_useCharacterUpVector = def.useCharacterUpVector;
    m->m_alignMode            = static_cast<std::int8_t>(def.alignMode);
    return m;
}

std::shared_ptr<BSCyclicBlendTransitionGenerator>
BehaviorBuilder::buildCyclicBlend(const BSCyclicBlendTransitionGeneratorDef& def) {
    auto cb = std::make_shared<BSCyclicBlendTransitionGenerator>();
    cb->m_userData = static_cast<std::uint64_t>(def.userData);
    cb->m_name     = def.name;
    cb->m_pBlenderGenerator = std::dynamic_pointer_cast<hkbGenerator>(buildNode(def.pBlenderGenerator));
    fillEventBase(cb->m_EventToFreezeBlendValue, def.eventToFreezeBlendValue);
    fillEventBase(cb->m_EventToCrossBlend, def.eventToCrossBlend);
    cb->m_fBlendParameter    = pf(def.fBlendParameter);
    cb->m_fTransitionDuration = pf(def.fTransitionDuration);
    cb->m_eBlendCurve        = static_cast<std::int8_t>(en(def.eBlendCurve, enums::BlendCurve()));
    if (def.bindings) cb->m_variableBindingSet = buildBindingSet(*def.bindings);
    return cb;
}

std::shared_ptr<BSBoneSwitchGenerator>
BehaviorBuilder::buildBoneSwitch(const BSBoneSwitchGeneratorDef& def) {
    auto bsg = std::make_shared<BSBoneSwitchGenerator>();
    bsg->m_userData = static_cast<std::uint64_t>(def.userData);
    bsg->m_name     = def.name;
    bsg->m_pDefaultGenerator = std::dynamic_pointer_cast<hkbGenerator>(buildNode(def.pDefaultGenerator));
    if (def.bindings) bsg->m_variableBindingSet = buildBindingSet(*def.bindings);
    if (def.children) {
        for (const auto& c : *def.children) {
            auto bone = std::make_shared<BSBoneSwitchGeneratorBoneData>();
            bone->m_pGenerator = std::dynamic_pointer_cast<hkbGenerator>(buildNode(c.pGenerator));
            if (c.boneWeights)   // present-but-empty must round-trip (see buildBlender)
                bone->m_spBoneWeight = buildBoneWeights(*c.boneWeights);
            if (c.bindings) bone->m_variableBindingSet = buildBindingSet(*c.bindings);
            bsg->m_ChildrenA.push_back(bone);
        }
    }
    return bsg;
}

// Generic modifiers — dispatch on class name. Each supported class is
// materialised as its concrete Tier-A type; the per-class params come from
// extraParams by name (the YAML key == the POCO member minus `m_`). The typed
// setters below map each GenericParam kind onto the field. Field lists mirror
// HKBuild's generic EmitGenericModifier over the same extracted params.
std::shared_ptr<hkbNode> BehaviorBuilder::buildGenericModifier(const std::string& id, const GenericModifierDef& def) {
    auto findScalar = [&](const char* key) -> std::optional<std::string> {
        for (const auto& p : def.extraParams)
            if (p.name == key && p.kind == GenericParamKind::Scalar && p.scalarValue)
                return p.scalarValue;
        return std::nullopt;
    };
    // Typed scalar setters — no-op when the param is absent (keeps POCO default).
    auto setF   = [&](const char* k, float& d)         { if (auto v = findScalar(k)) d = pf(*v); };
    auto setB   = [&](const char* k, bool& d)          { if (auto v = findScalar(k)) d = (*v == "true" || *v == "True" || *v == "1"); };
    auto setI16 = [&](const char* k, std::int16_t& d)  { if (auto v = findScalar(k)) d = static_cast<std::int16_t>(std::atoi(v->c_str())); };
    // Bone-index field: a bare number resolves directly (incl. a literal -1 = "no bone",
    // the engine-valid sentinel); a bone NAME resolves against the skeleton bone list
    // (data.boneNames), so a name-decompiled graph recompiles byte-exactly. A non-numeric
    // NAME that does NOT match the loaded skeleton is a real reference gone bad (empty or
    // mismatched skeleton) — do NOT silently emit -1 (it corrupts the ref and, in the
    // array case, crashes char-setup; BR-10). Throw so the graph fails compile and BR falls
    // back, and the log names the offending bone.
    auto setBone = [&](const char* k, std::int16_t& d) {
        auto v = findScalar(k);
        if (!v) return;
        const std::string& s = *v;
        char* end = nullptr;
        long  n   = std::strtol(s.c_str(), &end, 10);
        if (end != s.c_str() && *end == '\0') { d = static_cast<std::int16_t>(n); return; }  // numeric (incl. -1)
        const int idx = havok::cross::boneIndexByName(s, _data.boneNames);   // membrane: name -> index
        if (idx >= 0) { d = static_cast<std::int16_t>(idx); return; }
        throw std::runtime_error(std::string("BehaviorBuilder: bone field '") + k + "' name '" + s +
            "' is not in the loaded skeleton (empty or mismatched) — refusing to emit -1 (BR-10).");
    };
    auto setI32 = [&](const char* k, std::int32_t& d)  { if (auto v = findScalar(k)) d = static_cast<std::int32_t>(std::atoi(v->c_str())); };
    auto setU32 = [&](const char* k, std::uint32_t& d) { if (auto v = findScalar(k)) d = static_cast<std::uint32_t>(std::strtoul(v->c_str(), nullptr, 10)); };
    auto setV4  = [&](const char* k, Vector4& d)       { if (auto v = findScalar(k)) d = pv4(*v); };
    // Inline-event param -> an hkbEventProperty member.
    auto setEvent = [&](const char* k, hkbEventProperty& d) {
        for (const auto& p : def.extraParams)
            if (p.name == k && p.kind == GenericParamKind::InlineEvent && p.eventValue) { fillEventBase(d, *p.eventValue); return; }
    };
    // Single node reference (extracted as a plain scalar name) -> a modifier ptr.
    auto refMod = [&](const char* k) -> std::shared_ptr<hkbModifier> {
        if (auto v = findScalar(k); v && *v != "null" && !v->empty())
            return std::dynamic_pointer_cast<hkbModifier>(buildNode(*v));
        return nullptr;
    };
    // Common hkbModifier base fields (name/userData/enable/bindings).
    auto setBase = [&](auto& m) {
        m->m_userData = static_cast<std::uint64_t>(def.userData);
        m->m_name     = def.name;
        m->m_enable   = def.enable;
        if (def.bindings) m->m_variableBindingSet = buildBindingSet(*def.bindings);
    };

    if (def.className == "hkbTwistModifier") {
        auto tw = std::make_shared<hkbTwistModifier>();
        setBase(tw);
        setV4("axisOfRotation", tw->m_axisOfRotation);
        setF ("twistAngle",     tw->m_twistAngle);
        setBone("startBoneIndex", tw->m_startBoneIndex);
        setBone("endBoneIndex",   tw->m_endBoneIndex);
        if (auto v = findScalar("setAngleMethod"))          tw->m_setAngleMethod = static_cast<std::int8_t>(en(*v, enums::SetAngleMethod()));
        if (auto v = findScalar("rotationAxisCoordinates")) tw->m_rotationAxisCoordinates = static_cast<std::int8_t>(en(*v, enums::RotationAxisCoordinates()));
        setB ("isAdditive",     tw->m_isAdditive);
        return tw;
    }

    if (def.className == "hkbTimerModifier") {
        auto t = std::make_shared<hkbTimerModifier>();
        setBase(t);
        setF    ("alarmTimeSeconds", t->m_alarmTimeSeconds);
        setEvent("alarmEvent",       t->m_alarmEvent);
        return t;
    }

    if (def.className == "hkbDampingModifier") {
        auto d = std::make_shared<hkbDampingModifier>();
        setBase(d);
        setF("kP", d->m_kP); setF("kI", d->m_kI); setF("kD", d->m_kD);
        setB("enableScalarDamping", d->m_enableScalarDamping);
        setB("enableVectorDamping", d->m_enableVectorDamping);
        setF("rawValue", d->m_rawValue); setF("dampedValue", d->m_dampedValue);
        setV4("rawVector", d->m_rawVector); setV4("dampedVector", d->m_dampedVector);
        setV4("vecErrorSum", d->m_vecErrorSum); setV4("vecPreviousError", d->m_vecPreviousError);
        setF("errorSum", d->m_errorSum); setF("previousError", d->m_previousError);
        return d;
    }

    if (def.className == "hkbRotateCharacterModifier") {
        auto r = std::make_shared<hkbRotateCharacterModifier>();
        setBase(r);
        setF ("degreesPerSecond", r->m_degreesPerSecond);
        setF ("speedMultiplier",  r->m_speedMultiplier);
        setV4("axisOfRotation",   r->m_axisOfRotation);
        return r;
    }

    if (def.className == "hkbGetUpModifier") {
        auto g = std::make_shared<hkbGetUpModifier>();
        setBase(g);
        setV4 ("groundNormal", g->m_groundNormal);
        setF  ("duration", g->m_duration);
        setF  ("alignWithGroundDuration", g->m_alignWithGroundDuration);
        setBone("rootBoneIndex",    g->m_rootBoneIndex);
        setBone("otherBoneIndex",   g->m_otherBoneIndex);
        setBone("anotherBoneIndex", g->m_anotherBoneIndex);
        return g;
    }

    if (def.className == "BSDirectAtModifier") {
        auto d = std::make_shared<BSDirectAtModifier>();
        setBase(d);
        setB  ("directAtTarget", d->m_directAtTarget);
        setBone("sourceBoneIndex", d->m_sourceBoneIndex);
        setBone("startBoneIndex",  d->m_startBoneIndex);
        setBone("endBoneIndex",    d->m_endBoneIndex);
        setF  ("limitHeadingDegrees",  d->m_limitHeadingDegrees);
        setF  ("limitPitchDegrees",    d->m_limitPitchDegrees);
        setF  ("offsetHeadingDegrees", d->m_offsetHeadingDegrees);
        setF  ("offsetPitchDegrees",   d->m_offsetPitchDegrees);
        setF  ("onGain",  d->m_onGain);
        setF  ("offGain", d->m_offGain);
        setV4 ("targetLocation", d->m_targetLocation);
        setU32("userInfo", d->m_userInfo);
        setB  ("directAtCamera", d->m_directAtCamera);
        setF  ("directAtCameraX", d->m_directAtCameraX);
        setF  ("directAtCameraY", d->m_directAtCameraY);
        setF  ("directAtCameraZ", d->m_directAtCameraZ);
        setB  ("active", d->m_active);
        setF  ("currentHeadingOffset", d->m_currentHeadingOffset);
        setF  ("currentPitchOffset",   d->m_currentPitchOffset);
        return d;
    }

    if (def.className == "BSGetTimeStepModifier") {
        auto g = std::make_shared<BSGetTimeStepModifier>();
        setBase(g);
        setF("timeStep", g->m_timeStep);
        return g;
    }

    if (def.className == "hkbTransformVectorModifier") {
        auto m = std::make_shared<hkbTransformVectorModifier>();
        setBase(m);
        if (auto v = findScalar("rotation")) m->m_rotation = pq4(*v);
        setV4("translation", m->m_translation);
        setV4("vectorIn",    m->m_vectorIn);
        setV4("vectorOut",   m->m_vectorOut);
        setB("rotateOnly",        m->m_rotateOnly);
        setB("inverse",           m->m_inverse);
        setB("computeOnActivate", m->m_computeOnActivate);
        setB("computeOnModify",   m->m_computeOnModify);
        return m;
    }

    if (def.className == "BSDecomposeVectorModifier") {
        auto m = std::make_shared<BSDecomposeVectorModifier>();
        setBase(m);
        setV4("vector", m->m_vector);
        setF("x", m->m_x); setF("y", m->m_y); setF("z", m->m_z); setF("w", m->m_w);
        return m;
    }

    if (def.className == "BSLimbIKModifier") {
        auto m = std::make_shared<BSLimbIKModifier>();
        setBase(m);
        setF  ("limitAngleDegrees", m->m_limitAngleDegrees);
        setBone("startBoneIndex",    m->m_startBoneIndex);
        setBone("endBoneIndex",      m->m_endBoneIndex);
        setF  ("gain",       m->m_gain);
        setF  ("boneRadius", m->m_boneRadius);
        setF  ("castOffset", m->m_castOffset);
        return m;
    }

    if (def.className == "BSTweenerModifier") {
        auto m = std::make_shared<BSTweenerModifier>();
        setBase(m);
        setB("tweenPosition",    m->m_tweenPosition);
        setB("tweenRotation",    m->m_tweenRotation);
        setB("useTweenDuration", m->m_useTweenDuration);
        setF("tweenDuration",    m->m_tweenDuration);
        setV4("targetPosition",  m->m_targetPosition);
        if (auto v = findScalar("targetRotation")) m->m_targetRotation = pq4(*v);
        return m;
    }

    if (def.className == "BSPassByTargetTriggerModifier") {
        auto m = std::make_shared<BSPassByTargetTriggerModifier>();
        setBase(m);
        setV4("targetPosition",    m->m_targetPosition);
        setF ("radius",            m->m_radius);
        setV4("movementDirection", m->m_movementDirection);
        setEvent("triggerEvent",   m->m_triggerEvent);
        return m;
    }

    if (def.className == "BSTimerModifier") {
        auto m = std::make_shared<BSTimerModifier>();
        setBase(m);
        setF("alarmTimeSeconds", m->m_alarmTimeSeconds);
        setEvent("alarmEvent",   m->m_alarmEvent);
        setB("resetAlarm",       m->m_resetAlarm);
        return m;
    }

    if (def.className == "BSEventOnDeactivateModifier") {
        auto e = std::make_shared<BSEventOnDeactivateModifier>();
        setBase(e);
        setEvent("event", e->m_event);
        return e;
    }

    if (def.className == "BSEventOnFalseToTrueModifier") {
        auto e = std::make_shared<BSEventOnFalseToTrueModifier>();
        setBase(e);
        setB("bEnableEvent1", e->m_bEnableEvent1); setB("bVariableToTest1", e->m_bVariableToTest1); setEvent("EventToSend1", e->m_EventToSend1);
        setB("bEnableEvent2", e->m_bEnableEvent2); setB("bVariableToTest2", e->m_bVariableToTest2); setEvent("EventToSend2", e->m_EventToSend2);
        setB("bEnableEvent3", e->m_bEnableEvent3); setB("bVariableToTest3", e->m_bVariableToTest3); setEvent("EventToSend3", e->m_EventToSend3);
        return e;
    }

    if (def.className == "BSSpeedSamplerModifier") {
        auto s = std::make_shared<BSSpeedSamplerModifier>();
        setBase(s);
        setI32("state", s->m_state);
        setF  ("direction", s->m_direction);
        setF  ("goalSpeed", s->m_goalSpeed);
        setF  ("speedOut",  s->m_speedOut);
        return s;
    }

    if (def.className == "BSModifyOnceModifier") {
        auto m = std::make_shared<BSModifyOnceModifier>();
        setBase(m);
        m->m_pOnActivateModifier   = refMod("pOnActivateModifier");
        m->m_pOnDeactivateModifier = refMod("pOnDeactivateModifier");
        return m;
    }

    // Inline-object-list accessor: invoke fn(entry) for each map entry of a seq
    // param (e.g. bones:/eyeBones:/keyframeInfo:). `ef` reads a field of an entry.
    auto forEachObj = [&](const char* k, auto fn) {
        for (const auto& p : def.extraParams)
            if (p.name == k && p.kind == GenericParamKind::InlineObjectList && p.inlineObjectListValue)
                for (const auto& e : *p.inlineObjectListValue) fn(e);
    };
    auto ef = [](const GenericInlineObjectEntry& e, const char* k) -> std::optional<std::string> {
        for (const auto& [fk, fv] : e.fields) if (fk == k) return fv;
        return std::nullopt;
    };
    // NOTE on bone arrays: every shipping-tree instance of the ragdoll/keyframe
    // modifiers has `bones`/`keyframedBonesList`/`boneWeights: null`, so those
    // shared_ptr members stay null (POCO default). A non-null value would name an
    // hkbBoneIndexArray/hkbBoneWeightArray node — add resolution here if a tree
    // ever authors one (none do today). NOTE on controlData/worldFromModelModeData:
    // the generic param system forces any single YAML map into an InlineEvent, so
    // an inline *struct* member cannot round-trip through this path — vanilla
    // authors only defaults (the extracted `{event: …}` is noise against a struct
    // with no event field), so the structs are built at their POCO defaults.

    if (def.className == "BSRagdollContactListenerModifier") {
        auto m = std::make_shared<BSRagdollContactListenerModifier>();
        setBase(m);
        setEvent("contactEvent", m->m_contactEvent);
        m->m_bones = buildBoneIndexArray(id + "_bones");
        return m;
    }

    if (def.className == "hkbRigidBodyRagdollControlsModifier") {
        auto m = std::make_shared<hkbRigidBodyRagdollControlsModifier>();
        setBase(m);
        setF("durationToBlend", m->m_controlData.m_durationToBlend);
        auto& kfh = m->m_controlData.m_keyFrameHierarchyControlData;
        setF("hierarchyGain", kfh.m_hierarchyGain);
        setF("velocityDamping", kfh.m_velocityDamping);
        setF("accelerationGain", kfh.m_accelerationGain);
        setF("velocityGain", kfh.m_velocityGain);
        setF("positionGain", kfh.m_positionGain);
        setF("positionMaxLinearVelocity", kfh.m_positionMaxLinearVelocity);
        setF("positionMaxAngularVelocity", kfh.m_positionMaxAngularVelocity);
        setF("snapGain", kfh.m_snapGain);
        setF("snapMaxLinearVelocity", kfh.m_snapMaxLinearVelocity);
        setF("snapMaxAngularVelocity", kfh.m_snapMaxAngularVelocity);
        setF("snapMaxLinearDistance", kfh.m_snapMaxLinearDistance);
        setF("snapMaxAngularDistance", kfh.m_snapMaxAngularDistance);
        m->m_bones = buildBoneIndexArray(id + "_bones");
        return m;
    }

    if (def.className == "hkbPoweredRagdollControlsModifier") {
        auto m = std::make_shared<hkbPoweredRagdollControlsModifier>();
        setBase(m);
        setF("maxForce", m->m_controlData.m_maxForce);
        setF("tau",      m->m_controlData.m_tau);
        setF("damping",  m->m_controlData.m_damping);
        setF("proportionalRecoveryVelocity", m->m_controlData.m_proportionalRecoveryVelocity);
        setF("constantRecoveryVelocity",     m->m_controlData.m_constantRecoveryVelocity);
        auto& wfm = m->m_worldFromModelModeData;
        setI16("poseMatchingBone0", wfm.m_poseMatchingBone0);
        setI16("poseMatchingBone1", wfm.m_poseMatchingBone1);
        setI16("poseMatchingBone2", wfm.m_poseMatchingBone2);
        if (auto v = findScalar("worldFromModelMode"))
            wfm.m_mode = static_cast<std::int8_t>(std::atoi(v->c_str()));
        m->m_bones = buildBoneIndexArray(id + "_bones");
        return m;   // m_boneWeights null (no shipping tree authors one)
    }

    if (def.className == "hkbKeyframeBonesModifier") {
        auto m = std::make_shared<hkbKeyframeBonesModifier>();
        setBase(m);
        forEachObj("keyframeInfo", [&](const GenericInlineObjectEntry& e) {
            hkbKeyframeBonesModifierKeyframeInfo k;
            if (auto v = ef(e, "keyframedPosition")) k.m_keyframedPosition = pv4(*v);
            if (auto v = ef(e, "keyframedRotation")) k.m_keyframedRotation = pq4(*v);
            if (auto v = ef(e, "boneIndex"))         k.m_boneIndex = static_cast<std::int16_t>(std::atoi(v->c_str()));
            if (auto v = ef(e, "isValid"))           k.m_isValid = (*v == "true" || *v == "True" || *v == "1");
            m->m_keyframeInfo.push_back(k);
        });
        m->m_keyframedBonesList = buildBoneIndexArray(id + "_keyframedBonesList");
        return m;
    }

    if (def.className == "BSLookAtModifier") {
        auto m = std::make_shared<BSLookAtModifier>();
        setBase(m);
        setB("lookAtTarget", m->m_lookAtTarget);
        auto readBones = [&](const char* key, std::vector<BSLookAtModifierBoneData>& dst) {
            forEachObj(key, [&](const GenericInlineObjectEntry& e) {
                BSLookAtModifierBoneData b;
                if (auto v = ef(e, "index"))             b.m_index = static_cast<std::int16_t>(std::atoi(v->c_str()));
                if (auto v = ef(e, "fwdAxisLS"))         b.m_fwdAxisLS = pv4(*v);
                if (auto v = ef(e, "limitAngleDegrees")) b.m_limitAngleDegrees = pf(*v);
                if (auto v = ef(e, "onGain"))            b.m_onGain = pf(*v);
                if (auto v = ef(e, "offGain"))           b.m_offGain = pf(*v);
                if (auto v = ef(e, "enabled"))           b.m_enabled = (*v == "true" || *v == "True" || *v == "1");
                dst.push_back(b);
            });
        };
        readBones("bones",    m->m_bones);
        readBones("eyeBones", m->m_eyeBones);
        setF("limitAngleDegrees",          m->m_limitAngleDegrees);
        setF("limitAngleThresholdDegrees", m->m_limitAngleThresholdDegrees);
        setB("continueLookOutsideOfLimit", m->m_continueLookOutsideOfLimit);
        setF("onGain",  m->m_onGain);
        setF("offGain", m->m_offGain);
        setB("useBoneGains", m->m_useBoneGains);
        setV4("targetLocation", m->m_targetLocation);
        setB("targetOutsideLimits", m->m_targetOutsideLimits);
        setEvent("targetOutOfLimitEvent", m->m_targetOutOfLimitEvent);
        setB("lookAtCamera", m->m_lookAtCamera);
        setF("lookAtCameraX", m->m_lookAtCameraX);
        setF("lookAtCameraY", m->m_lookAtCameraY);
        setF("lookAtCameraZ", m->m_lookAtCameraZ);
        return m;
    }

    // Unknown generic modifier class outside the supported subset: emit a base
    // hkbModifier carrying the common fields so the graph stays well-formed.
    auto m = std::make_shared<hkbModifier>();
    setBase(m);
    return m;
}

// ── Tier-B kinds added with the transpiler class expansion ────────────────────
// Each mirrors the corresponding Emit*() in HKBuild's BehaviorXmlEmitter: same
// field set, same reference resolution (child generators/modifiers via buildNode,
// events via resolveEventId, float-strings via pf), in the same order.

// hkbModifierGenerator (EmitModifierGenerator): a generator that wraps a modifier
// applied to a child generator. Note: base is hkbGenerator (no `enable`), so the
// emitter writes only variableBindingSet/userData/name/modifier/generator.
std::shared_ptr<hkbModifierGenerator>
BehaviorBuilder::buildModifierGenerator(const ModifierGeneratorDef& def) {
    auto mg = std::make_shared<hkbModifierGenerator>();
    mg->m_userData  = static_cast<std::uint64_t>(def.userData);
    mg->m_name      = def.name;
    mg->m_modifier  = std::dynamic_pointer_cast<hkbModifier>(buildNode(def.modifier));
    mg->m_generator = std::dynamic_pointer_cast<hkbGenerator>(buildNode(def.generator));
    if (def.bindings) mg->m_variableBindingSet = buildBindingSet(*def.bindings);
    return mg;
}

// BSOffsetAnimationGenerator (EmitOffsetAnimGenerator): default generator + offset
// clip generator + three offset floats.
std::shared_ptr<BSOffsetAnimationGenerator>
BehaviorBuilder::buildOffsetAnim(const BSOffsetAnimationGeneratorDef& def) {
    auto oag = std::make_shared<BSOffsetAnimationGenerator>();
    oag->m_userData             = static_cast<std::uint64_t>(def.userData);
    oag->m_name                 = def.name;
    oag->m_pDefaultGenerator    = std::dynamic_pointer_cast<hkbGenerator>(buildNode(def.pDefaultGenerator));
    oag->m_pOffsetClipGenerator = std::dynamic_pointer_cast<hkbGenerator>(buildNode(def.pOffsetClipGenerator));
    oag->m_fOffsetVariable      = pf(def.fOffsetVariable);
    oag->m_fOffsetRangeStart    = pf(def.fOffsetRangeStart);
    oag->m_fOffsetRangeEnd      = pf(def.fOffsetRangeEnd);
    if (def.bindings) oag->m_variableBindingSet = buildBindingSet(*def.bindings);
    return oag;
}

// hkbModifierList (EmitModifierList): an hkbModifier holding an ordered list of
// child modifiers.
std::shared_ptr<hkbModifierList>
BehaviorBuilder::buildModifierList(const ModifierListDef& def) {
    auto ml = std::make_shared<hkbModifierList>();
    ml->m_userData = static_cast<std::uint64_t>(def.userData);
    ml->m_name     = def.name;
    ml->m_enable   = def.enable;
    if (def.bindings) ml->m_variableBindingSet = buildBindingSet(*def.bindings);
    for (const auto& modName : def.modifiers) {
        if (modName.empty() || modName == "null") continue;  // mirror emitter
        ml->m_modifiers.push_back(std::dynamic_pointer_cast<hkbModifier>(buildNode(modName)));
    }
    return ml;
}

// BSIsActiveModifier (EmitBSIsActiveModifier): 5 active/invert bool pairs.
std::shared_ptr<BSIsActiveModifier>
BehaviorBuilder::buildIsActiveModifier(const BSIsActiveModifierDef& def) {
    auto iam = std::make_shared<BSIsActiveModifier>();
    iam->m_userData = static_cast<std::uint64_t>(def.userData);
    iam->m_name     = def.name;
    iam->m_enable   = def.enable;
    iam->m_bIsActive0 = def.bIsActive0; iam->m_bInvertActive0 = def.bInvertActive0;
    iam->m_bIsActive1 = def.bIsActive1; iam->m_bInvertActive1 = def.bInvertActive1;
    iam->m_bIsActive2 = def.bIsActive2; iam->m_bInvertActive2 = def.bInvertActive2;
    iam->m_bIsActive3 = def.bIsActive3; iam->m_bInvertActive3 = def.bInvertActive3;
    iam->m_bIsActive4 = def.bIsActive4; iam->m_bInvertActive4 = def.bInvertActive4;
    if (def.bindings) iam->m_variableBindingSet = buildBindingSet(*def.bindings);
    return iam;
}

// BSSynchronizedClipGenerator (EmitSynchronizedClipGenerator): wraps a clip
// generator with synchronization parameters.
std::shared_ptr<BSSynchronizedClipGenerator>
BehaviorBuilder::buildSynchronizedClip(const BSSynchronizedClipGeneratorDef& def) {
    auto sc = std::make_shared<BSSynchronizedClipGenerator>();
    sc->m_userData       = static_cast<std::uint64_t>(def.userData);
    sc->m_name           = def.name;
    sc->m_pClipGenerator = std::dynamic_pointer_cast<hkbGenerator>(buildNode(def.pClipGenerator));
    sc->m_SyncAnimPrefix = def.syncAnimPrefix;
    sc->m_bSyncClipIgnoreMarkPlacement = def.bSyncClipIgnoreMarkPlacement;
    sc->m_fGetToMarkTime       = pf(def.fGetToMarkTime);
    sc->m_fMarkErrorThreshold  = pf(def.fMarkErrorThreshold);
    sc->m_bLeadCharacter       = def.bLeadCharacter;
    sc->m_bReorientSupportChar = def.bReorientSupportChar;
    sc->m_bApplyMotionFromRoot = def.bApplyMotionFromRoot;
    sc->m_sAnimationBindingIndex = static_cast<std::int16_t>(def.sAnimationBindingIndex);
    if (def.bindings) sc->m_variableBindingSet = buildBindingSet(*def.bindings);
    return sc;
}

// hkbPoseMatchingGenerator (EmitPoseMatchingGenerator): an hkbBlenderGenerator
// (children + blend params, like buildBlender) plus pose-matching params.
std::shared_ptr<hkbPoseMatchingGenerator>
BehaviorBuilder::buildPoseMatching(const PoseMatchingGeneratorDef& def) {
    // PoseMatchingGenerator.Mode enum (no shared table — local): MODE_MATCH=0, MODE_PLAY=1.
    static const std::unordered_map<std::string, long> kPoseMatchingMode = {
        {"MODE_MATCH", 0}, {"MODE_PLAY", 1},
    };

    auto pmg = std::make_shared<hkbPoseMatchingGenerator>();
    // hkbBlenderGenerator base fields (mirrors buildBlender).
    pmg->m_userData = static_cast<std::uint64_t>(def.userData);
    pmg->m_name     = def.name;
    pmg->m_referencePoseWeightThreshold = pf(def.referencePoseWeightThreshold);
    pmg->m_blendParameter               = pf(def.blendParameter);
    pmg->m_minCyclicBlendParameter      = pf(def.minCyclicBlendParameter);
    pmg->m_maxCyclicBlendParameter      = pf(def.maxCyclicBlendParameter);
    pmg->m_indexOfSyncMasterChild       = static_cast<std::int16_t>(def.indexOfSyncMasterChild);
    // hkbBlenderGenerator flags (this is a hkbBlenderGenerator subclass) — NOT the transition-
    // effect FlagBits table (blender FLAG_SYNC=1, not 2). Numeric today, so this only matters
    // for a named flag, but the right table is the right table.
    pmg->m_flags             = static_cast<std::int16_t>(en(def.flags, enums::BlenderFlags()));
    pmg->m_subtractLastChild = def.subtractLastChild;
    if (def.bindings) pmg->m_variableBindingSet = buildBindingSet(*def.bindings);
    if (def.children) {
        for (const auto& c : *def.children) {
            if (c.generator.empty() || c.generator == "null") continue;  // mirror emitter
            auto child = std::make_shared<hkbBlenderGeneratorChild>();
            child->m_generator = std::dynamic_pointer_cast<hkbGenerator>(buildNode(c.generator));
            // Build whenever a boneWeights block is present — a present-but-empty (count 0) array
            // must round-trip to a non-null empty array, not null (mirrors buildBlender; the old
            // HasData() gate silently dropped vanilla pose-matcher arrays).
            if (c.boneWeights)
                child->m_boneWeights = buildBoneWeights(*c.boneWeights);
            child->m_weight               = pf(c.weight);
            child->m_worldFromModelWeight = pf(c.worldFromModelWeight);
            if (c.bindings) child->m_variableBindingSet = buildBindingSet(*c.bindings);
            pmg->m_children.push_back(child);
        }
    }
    // hkbPoseMatchingGenerator-specific fields.
    pmg->m_worldFromModelRotation = pq4(def.worldFromModelRotation);  // (x y z w) -> Quaternion
    pmg->m_blendSpeed             = pf(def.blendSpeed);
    pmg->m_minSpeedToSwitch       = pf(def.minSpeedToSwitch);
    pmg->m_minSwitchTimeNoError   = pf(def.minSwitchTimeNoError);
    pmg->m_minSwitchTimeFullError = pf(def.minSwitchTimeFullError);
    pmg->m_startPlayingEventId    = resolveEventId(def.startPlayingEvent, def.startPlayingEventId);
    pmg->m_startMatchingEventId   = resolveEventId(def.startMatchingEvent, def.startMatchingEventId);
    pmg->m_rootBoneIndex    = static_cast<std::int16_t>(def.rootBoneIndex);
    pmg->m_otherBoneIndex   = static_cast<std::int16_t>(def.otherBoneIndex);
    pmg->m_anotherBoneIndex = static_cast<std::int16_t>(def.anotherBoneIndex);
    pmg->m_pelvisIndex      = static_cast<std::int16_t>(def.pelvisIndex);
    pmg->m_mode = static_cast<std::int8_t>(en(def.mode, kPoseMatchingMode));
    return pmg;
}

// hkbEventDrivenModifier (EmitEventDrivenModifier): an hkbModifierWrapper (wraps a
// single modifier) gated by activate/deactivate events.
std::shared_ptr<hkbEventDrivenModifier>
BehaviorBuilder::buildEventDrivenModifier(const EventDrivenModifierDef& def) {
    auto edm = std::make_shared<hkbEventDrivenModifier>();
    edm->m_userData = static_cast<std::uint64_t>(def.userData);
    edm->m_name     = def.name;
    edm->m_enable   = def.enable;
    edm->m_modifier = std::dynamic_pointer_cast<hkbModifier>(buildNode(def.modifier));  // hkbModifierWrapper::m_modifier
    edm->m_activateEventId   = resolveEventId(def.activateEvent, def.activateEventId);
    edm->m_deactivateEventId = resolveEventId(def.deactivateEvent, def.deactivateEventId);
    edm->m_activeByDefault   = def.activeByDefault;
    if (def.bindings) edm->m_variableBindingSet = buildBindingSet(*def.bindings);
    return edm;
}

// BSEventEveryNEventsModifier (EmitBSEventEveryNEventsModifier): inline
// eventToCheckFor / eventToSend event properties + counters.
std::shared_ptr<BSEventEveryNEventsModifier>
BehaviorBuilder::buildEventEveryN(const BSEventEveryNEventsModifierDef& def) {
    auto een = std::make_shared<BSEventEveryNEventsModifier>();
    een->m_userData = static_cast<std::uint64_t>(def.userData);
    een->m_name     = def.name;
    een->m_enable   = def.enable;
    fillEventBase(een->m_eventToCheckFor, def.eventToCheckFor);
    fillEventBase(een->m_eventToSend, def.eventToSend);
    een->m_numberOfEventsBeforeSend        = static_cast<std::int8_t>(def.numberOfEventsBeforeSend);
    een->m_minimumNumberOfEventsBeforeSend = static_cast<std::int8_t>(def.minimumNumberOfEventsBeforeSend);
    een->m_randomizeNumberOfEvents         = def.randomizeNumberOfEvents;
    if (def.bindings) een->m_variableBindingSet = buildBindingSet(*def.bindings);
    return een;
}

// BSInterpValueModifier: a scalar interp/damp — four floats + base, no child refs.
std::shared_ptr<BSInterpValueModifier>
BehaviorBuilder::buildInterpValue(const BSInterpValueModifierDef& def) {
    auto m = std::make_shared<BSInterpValueModifier>();
    m->m_userData = static_cast<std::uint64_t>(def.userData);
    m->m_name     = def.name;
    m->m_enable   = def.enable;
    m->m_source   = pf(def.source);
    m->m_target   = pf(def.target);
    m->m_result   = pf(def.result);
    m->m_gain     = pf(def.gain);
    if (def.bindings) m->m_variableBindingSet = buildBindingSet(*def.bindings);
    return m;
}

// hkbEventRangeDataArray: referenced array of (upperBound, event, mode) rows.
std::shared_ptr<hkbEventRangeDataArray>
BehaviorBuilder::buildEventRangeDataArray(const EventRangeDataArrayDef& def) {
    static const std::unordered_map<std::string, long> kEventRangeMode = {
        {"EVENT_MODE_SEND_ONCE", 0}, {"EVENT_MODE_SEND_ON_TRUE", 1},
        {"EVENT_MODE_SEND_ON_FALSE_TO_TRUE", 2}, {"EVENT_MODE_SEND_EVERY_FRAME_ONCE_TRUE", 3},
    };
    auto arr = std::make_shared<hkbEventRangeDataArray>();
    for (const auto& e : def.eventData) {
        hkbEventRangeData out;
        out.m_upperBound = pf(e.upperBound);
        fillEventBase(out.m_event, e.event, e.eventId, e.payload.value_or(std::string{}));
        out.m_eventMode = static_cast<std::int8_t>(en(e.eventMode, kEventRangeMode));
        arr->m_eventData.push_back(std::move(out));
    }
    return arr;
}

// hkbEventsFromRangeModifier: input compared against the data array's ranges; the
// array is referenced by the modifier's name (or the eventRanges field if set).
std::shared_ptr<hkbEventsFromRangeModifier>
BehaviorBuilder::buildEventsFromRange(const std::string& id, const EventsFromRangeModifierDef& def) {
    auto m = std::make_shared<hkbEventsFromRangeModifier>();
    m->m_userData   = static_cast<std::uint64_t>(def.userData);
    m->m_name       = def.name;
    m->m_enable     = def.enable;
    m->m_inputValue = pf(def.inputValue);
    m->m_lowerBound = pf(def.lowerBound);
    // Sidecar: explicit eventRanges id if set, else the id-convention <id>_eventRanges.
    const std::string arrName =
        (def.eventRanges && *def.eventRanges != "null" && !def.eventRanges->empty()) ? *def.eventRanges : id + "_eventRanges";
    if (auto it = _data.eventRangeDataArrays.find(arrName); it != _data.eventRangeDataArrays.end())
        m->m_eventRanges = buildEventRangeDataArray(it->second);
    if (def.bindings) m->m_variableBindingSet = buildBindingSet(*def.bindings);
    return m;
}

// hkbEvaluateExpressionModifier (EmitEvaluateExpressionModifier): references an
// hkbExpressionDataArray by name (built inline here, matching the emitter, which
// emits the array as a separate object).
std::shared_ptr<hkbEvaluateExpressionModifier>
BehaviorBuilder::buildEvaluateExpression(const std::string& id, const EvaluateExpressionModifierDef& def) {
    auto eem = std::make_shared<hkbEvaluateExpressionModifier>();
    eem->m_userData = static_cast<std::uint64_t>(def.userData);
    eem->m_name     = def.name;
    eem->m_enable   = def.enable;
    // Sidecar: explicit expressions id if set, else the id-convention <id>_expressions.
    const std::string arrName =
        (!def.expressions.empty() && def.expressions != "null") ? def.expressions : id + "_expressions";
    if (auto it = _data.expressionDataArrays.find(arrName); it != _data.expressionDataArrays.end())
        eem->m_expressions = buildExpressionDataArray(it->second);
    if (def.bindings) eem->m_variableBindingSet = buildBindingSet(*def.bindings);
    return eem;
}

// hkbExpressionDataArray (EmitExpressionDataArray): array of expression records.
std::shared_ptr<hkbExpressionDataArray>
BehaviorBuilder::buildExpressionDataArray(const ExpressionDataArrayDef& def) {
    auto eda = std::make_shared<hkbExpressionDataArray>();
    for (const auto& e : def.expressionsData) {
        hkbExpressionData out;
        out.m_expression = e.expression;
        out.m_assignmentVariableIndex = resolveVariableIndex(e.assignmentVariable, e.assignmentVariableIndex);
        out.m_assignmentEventIndex    = resolveEventId(e.assignmentEvent, e.assignmentEventIndex);
        out.m_eventMode = static_cast<std::int8_t>(en(e.eventMode, enums::ExpressionEventMode()));
        eda->m_expressionsData.push_back(std::move(out));
    }
    return eda;
}

// hkbFootIkControlsModifier (EmitFootIkControlsModifier): inline controlData
// (gains struct) + a legs array + error/align vectors.
std::shared_ptr<hkbFootIkControlsModifier>
BehaviorBuilder::buildFootIkControls(const FootIkControlsModifierDef& def) {
    auto ficm = std::make_shared<hkbFootIkControlsModifier>();
    ficm->m_userData = static_cast<std::uint64_t>(def.userData);
    ficm->m_name     = def.name;
    ficm->m_enable   = def.enable;

    // controlData.gains
    const auto& g = def.controlData.gains;
    auto& og = ficm->m_controlData.m_gains;
    og.m_onOffGain                 = g.onOffGain;
    og.m_groundAscendingGain       = g.groundAscendingGain;
    og.m_groundDescendingGain      = g.groundDescendingGain;
    og.m_footPlantedGain           = g.footPlantedGain;
    og.m_footRaisedGain            = g.footRaisedGain;
    og.m_footUnlockGain            = g.footUnlockGain;
    og.m_worldFromModelFeedbackGain = g.worldFromModelFeedbackGain;
    og.m_errorUpDownBias           = g.errorUpDownBias;
    og.m_alignWorldFromModelGain   = g.alignWorldFromModelGain;
    og.m_hipOrientationGain        = g.hipOrientationGain;
    og.m_maxKneeAngleDifference    = g.maxKneeAngleDifference;
    og.m_ankleOrientationGain      = g.ankleOrientationGain;

    // legs
    if (def.legs) {
        for (const auto& leg : *def.legs) {
            hkbFootIkControlsModifierLeg out;
            out.m_groundPosition = pv4(leg.groundPosition);
            if (leg.ungroundedEvent) fillEventBase(out.m_ungroundedEvent, *leg.ungroundedEvent);
            out.m_verticalError = leg.verticalError;
            out.m_hitSomething  = leg.hitSomething;
            out.m_isPlantedMS   = leg.isPlantedMS;
            ficm->m_legs.push_back(std::move(out));
        }
    }

    ficm->m_errorOutTranslation     = pv4(def.errorOutTranslation);
    ficm->m_alignWithGroundRotation = pq4(def.alignWithGroundRotation);  // (x y z w) -> Quaternion
    if (def.bindings) ficm->m_variableBindingSet = buildBindingSet(*def.bindings);
    return ficm;
}

// ── Node dispatch ─────────────────────────────────────────────────────────────
std::shared_ptr<hkbNode> BehaviorBuilder::buildNode(const std::string& name) {
    if (name.empty() || name == "null") return nullptr;

    auto memo = _nodeMemo.find(name);
    if (memo != _nodeMemo.end()) return memo->second;

    std::shared_ptr<hkbNode> result;

    if (auto it = _data.clips.find(name); it != _data.clips.end())
        result = buildClip(it->second);
    else if (auto it2 = _data.blenders.find(name); it2 != _data.blenders.end())
        result = buildBlender(it2->second);
    else if (auto it3 = _data.selectors.find(name); it3 != _data.selectors.end())
        result = buildSelector(it3->second);
    else if (auto it4 = _data.stateMachines.find(name); it4 != _data.stateMachines.end())
        result = buildStateMachine(it4->second);
    else if (auto it7 = _data.stateTaggingGenerators.find(name); it7 != _data.stateTaggingGenerators.end())
        result = buildStateTagging(it7->second);
    else if (auto it8 = _data.behaviorReferences.find(name); it8 != _data.behaviorReferences.end())
        result = buildBehaviorReference(it8->second);
    else if (auto it8b = _data.gamebryoSequences.find(name); it8b != _data.gamebryoSequences.end())
        result = buildGamebryoSequence(it8b->second);
    else if (auto it10 = _data.cyclicBlendGenerators.find(name); it10 != _data.cyclicBlendGenerators.end())
        result = buildCyclicBlend(it10->second);
    else if (auto it11 = _data.boneSwitchGenerators.find(name); it11 != _data.boneSwitchGenerators.end())
        result = buildBoneSwitch(it11->second);
    else if (auto it12 = _data.genericModifiers.find(name); it12 != _data.genericModifiers.end())
        result = buildGenericModifier(name, it12->second);
    // Tier-B kinds added with the transpiler class expansion (formerly stubbed):
    else if (auto it13 = _data.modifierGenerators.find(name); it13 != _data.modifierGenerators.end())
        result = buildModifierGenerator(it13->second);
    else if (auto it14 = _data.offsetAnimGenerators.find(name); it14 != _data.offsetAnimGenerators.end())
        result = buildOffsetAnim(it14->second);
    else if (auto it15 = _data.modifierLists.find(name); it15 != _data.modifierLists.end())
        result = buildModifierList(it15->second);
    else if (auto it16 = _data.isActiveModifiers.find(name); it16 != _data.isActiveModifiers.end())
        result = buildIsActiveModifier(it16->second);
    else if (auto it17 = _data.synchronizedClips.find(name); it17 != _data.synchronizedClips.end())
        result = buildSynchronizedClip(it17->second);
    else if (auto it18 = _data.poseMatchingGenerators.find(name); it18 != _data.poseMatchingGenerators.end())
        result = buildPoseMatching(it18->second);
    else if (auto it19 = _data.eventDrivenModifiers.find(name); it19 != _data.eventDrivenModifiers.end())
        result = buildEventDrivenModifier(it19->second);
    else if (auto it20 = _data.eventEveryNModifiers.find(name); it20 != _data.eventEveryNModifiers.end())
        result = buildEventEveryN(it20->second);
    else if (auto it21 = _data.evaluateExpressionModifiers.find(name); it21 != _data.evaluateExpressionModifiers.end())
        result = buildEvaluateExpression(name, it21->second);
    else if (auto it22 = _data.footIkControlsModifiers.find(name); it22 != _data.footIkControlsModifiers.end())
        result = buildFootIkControls(it22->second);
    else if (auto it23 = _data.interpValueModifiers.find(name); it23 != _data.interpValueModifiers.end())
        result = buildInterpValue(it23->second);
    else if (auto it24 = _data.eventsFromRangeModifiers.find(name); it24 != _data.eventsFromRangeModifiers.end())
        result = buildEventsFromRange(name, it24->second);
    else if (auto it25 = _data.referencePoseGenerators.find(name); it25 != _data.referencePoseGenerators.end())
        result = buildReferencePose(it25->second);
    else if (auto it26 = _data.iStateManagerModifiers.find(name); it26 != _data.iStateManagerModifiers.end())
        result = buildIStateManager(it26->second);
    else if (auto it27 = _data.footIkModifiers.find(name); it27 != _data.footIkModifiers.end())
        result = buildFootIkModifier(it27->second);

    if (result) _nodeMemo[name] = result;
    return result;
}

// ── Build root ────────────────────────────────────────────────────────────────
std::shared_ptr<hkRootLevelContainer> BehaviorBuilder::Build() {
    _graphData = buildGraphData();

    const std::string& rootName = _data.behavior.behavior.rootGenerator;
    auto rootGen = std::dynamic_pointer_cast<hkbGenerator>(buildNode(rootName));

    auto graph = std::make_shared<hkbBehaviorGraph>();
    graph->m_name = _data.behavior.behavior.name;
    graph->m_variableMode =
        static_cast<std::int8_t>(en(_data.behavior.behavior.variableMode, enums::VariableMode()));
    graph->m_rootGenerator = rootGen;
    graph->m_data = _graphData;

    auto root = std::make_shared<hkRootLevelContainer>();
    hkRootLevelContainerNamedVariant nv;
    nv.m_name      = "hkbBehaviorGraph";
    nv.m_className = "hkbBehaviorGraph";
    nv.m_variant   = graph;
    root->m_namedVariants.push_back(nv);
    return root;
}

// ── Stage 4: bindings-resolve pass ──────────────────────────────────────────────
// Pre-resolves NAMED variable/character-property bindings to indices and clears the
// name, so the builder reads pre-resolved indices. Mirrors resolveVariableIndex /
// resolveCharPropIndex (name path only) — the name->index map is context-free, so the
// resolution can run as a discrete pass over the merged BehaviorData. Numeric bindings
// are left untouched (their OOB check stays in resolveVariableIndex at build time).
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
