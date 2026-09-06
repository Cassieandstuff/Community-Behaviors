// havok-core — Tier B builder round-trip. Construct a small behavior as *Def
// objects directly (NO YAML parsing), run it through BehaviorBuilder ->
// PackFileSerializer -> bytes -> PackFileDeserializer, and CHECK the reconstructed
// Tier-A graph has the expected structure/values. Self-verifying in the same
// style as tests/roundtrip_test.cpp: Build and (de)serialize are independently
// derived, so a disagreement fails here without any external oracle.

#include "test_harness.h"

#include "havok/classes/Classes.h"
#include "havok/core/PackFileDeserializer.h"
#include "havok/core/PackFileSerializer.h"
#include "havok/model/BehaviorBuilder.h"
#include "havok/model/BehaviorData.h"

#include <exception>
#include <memory>
#include <string>

using namespace havok;
using namespace havok::model;

namespace {

// Build a mini-TrueFlight BehaviorData with the node kinds the builder supports:
//   graph data (variables + events)
//   root SM (TrueFlightSM) with isActive binding + 1 state + 1 wildcard transition
//   state ST_Hover -> generator TF_DirectionCyclic (BSCyclicBlendTransitionGenerator)
//     cyclic wraps TF_Direction_Blend (hkbBlenderGenerator)
//       blender has 2 clip children (CLIP_Fwd, CLIP_Right) + a named bone-weight
//     state has an enter-notify event + a transition (toState resolved to id)
//   transition effect FlightBlend
//   a generic hkbTwistModifier (referenced as the blender's... no — kept standalone-
//     reachable by attaching it as a modifier-less node is not possible without a
//     modifier-generator, so we exercise it via buildNode directly in a second pass).
BehaviorData makeData() {
    BehaviorData d;
    d.behavior.behavior.name          = "TestBehavior.hkb";
    d.behavior.behavior.variableMode  = "VARIABLE_MODE_MAINTAIN_VALUES_WHEN_INACTIVE";  // -> 1
    d.behavior.behavior.rootGenerator = "TrueFlightSM";
    d.behavior.behavior.data          = "graphdata";

    // ── skeleton (for named bone weights) ──
    d.boneNames = {"NPC Root [Root]", "NPC Pelvis [Pelv]", "NPC Spine [Spn0]", "NPC Head [Head]"};

    // ── graph data ──
    BehaviorGraphDataDef gd;
    gd.variables.push_back({"TF_IsFlying",     "VARIABLE_TYPE_BOOL", "ROLE_DEFAULT", 0, 0, std::nullopt});
    gd.variables.push_back({"TF_HoverDirection","VARIABLE_TYPE_REAL", "ROLE_DEFAULT", 0, 0, std::nullopt});
    gd.variables.push_back({"Pitch",            "VARIABLE_TYPE_REAL", "ROLE_DEFAULT", 0, 0, std::nullopt});
    gd.events.push_back({"TF_FlightStart", "0"});
    gd.events.push_back({"TF_HoverLand",   "FLAG_SYNC_POINT"});  // -> flags 2 (Havok FLAG_SYNC_POINT=2; FLAG_SILENT=1)
    d.graphData = gd;

    // ── clips ──
    ClipGeneratorDef cFwd;
    cFwd.name = "CLIP_Fwd";
    cFwd.animationName = "Animations\\Fwd.hkx";
    cFwd.mode = "MODE_LOOPING";          // -> 1
    cFwd.playbackSpeed = "1.250000";
    cFwd.startTime = "0.000000";
    // one trigger referencing an event by name + a payload
    ClipTriggerDef trig;
    trig.localTime = "0.500000";
    trig.event = "TF_HoverLand";
    trig.payload = "PayloadA";
    trig.relativeToEndOfClip = true;
    cFwd.triggers = std::vector<ClipTriggerDef>{trig};
    d.clips[cFwd.name] = cFwd;

    ClipGeneratorDef cRight;
    cRight.name = "CLIP_Right";
    cRight.animationName = "Animations\\Right.hkx";
    cRight.mode = "MODE_LOOPING";
    cRight.playbackSpeed = "1.000000";
    d.clips[cRight.name] = cRight;

    // ── blender with named bone weights on first child ──
    BlenderGeneratorDef blend;
    blend.name = "TF_Direction_Blend";
    blend.flags = 49;
    blend.blendParameter = "0.000000";
    BlenderChildDef bc0;
    bc0.generator = "CLIP_Fwd";
    bc0.weight = "0.000000";
    bc0.worldFromModelWeight = "1.000000";
    BoneWeightsDef bw;
    bw.named = std::map<std::string, std::string>{
        {"NPC Spine [Spn0]", "1.000000"}, {"NPC Head [Head]", "1.000000"}};
    bc0.boneWeights = bw;
    BlenderChildDef bc1;
    bc1.generator = "CLIP_Right";
    bc1.weight = "0.250000";
    bc1.worldFromModelWeight = "1.000000";
    blend.children = {bc0, bc1};
    d.blenders[blend.name] = blend;

    // ── cyclic blend wrapping the blender, with fBlendParameter binding ──
    BSCyclicBlendTransitionGeneratorDef cyc;
    cyc.name = "TF_DirectionCyclic";
    cyc.pBlenderGenerator = "TF_Direction_Blend";
    cyc.eventToFreezeBlendValue.event = "TF_HoverLand";
    cyc.fTransitionDuration = "0.200000";
    BindingDef cb;
    cb.memberPath = "fBlendParameter";
    cb.variable = "TF_HoverDirection";
    cyc.bindings = std::vector<BindingDef>{cb};
    d.cyclicBlendGenerators[cyc.name] = cyc;

    // ── transition effect ──
    TransitionEffectDef eff;
    eff.name = "FlightBlend";
    eff.selfTransitionMode = "SELF_TRANSITION_MODE_CONTINUE_IF_CYCLIC_BLEND_IF_ACYCLIC";  // -> 0
    eff.duration = "0.300000";
    eff.flags = "0";
    eff.endMode = "END_MODE_NONE";
    eff.blendCurve = "BLEND_CURVE_SMOOTH";
    d.transitionEffects[eff.name] = eff;

    // ── state ST_Hover ──
    StateDef st;
    st.name = "ST_Hover";
    st.stateId = 0;
    st.generator = "TF_DirectionCyclic";
    // enter-notify event
    EventPropertyDef notify;
    notify.event = "TF_FlightStart";
    st.enterNotifyEvents = std::vector<EventPropertyDef>{notify};
    d.states[st.name] = st;

    // a second state to be a transition target
    StateDef st2;
    st2.name = "ST_Flight";
    st2.stateId = 1;
    st2.generator = "CLIP_Right";
    d.states[st2.name] = st2;

    // ── root SM ──
    StateMachineDef sm;
    sm.name = "TrueFlightSM";
    sm.startStateId = 0;
    sm.selfTransitionMode = "SELF_TRANSITION_MODE_NO_TRANSITION";  // -> 0
    BindingDef smBind;
    smBind.memberPath = "isActive";
    smBind.variable = "TF_IsFlying";
    sm.bindings = std::vector<BindingDef>{smBind};
    sm.states = {"ST_Hover", "ST_Flight"};
    // wildcard transition: event TF_FlightStart -> toState ST_Flight (resolved to id 1)
    TransitionInfoDef wt;
    wt.event = "TF_FlightStart";
    wt.toState = "ST_Flight";
    wt.toStateId = 1;  // (loader resolves this; we set directly here)
    wt.transition = "FlightBlend";
    wt.flags = "FLAG_IS_LOCAL_WILDCARD|FLAG_DISABLE_CONDITION";  // -> 2048|256 = 2304
    sm.parsedWildcardTransitions = std::vector<TransitionInfoDef>{wt};
    d.stateMachines[sm.name] = sm;

    // ── a generic hkbTwistModifier (exercised standalone via buildNode below) ──
    GenericModifierDef tw;
    tw.className = "hkbTwistModifier";
    tw.name = "TFA_BowSpineTwist";
    tw.userData = 1;
    tw.enable = true;
    BindingDef twBind;
    twBind.memberPath = "twistAngle";
    twBind.variable = "Pitch";
    tw.bindings = std::vector<BindingDef>{twBind};
    auto addScalar = [&](const char* k, const char* v) {
        GenericParam p; p.name = k; p.kind = GenericParamKind::Scalar; p.scalarValue = v;
        tw.extraParams.push_back(p);
    };
    addScalar("axisOfRotation", "(1.000000 0.000000 0.000000 0.000000)");
    addScalar("twistAngle", "0.000000");
    addScalar("startBoneIndex", "24");
    addScalar("endBoneIndex", "26");
    addScalar("setAngleMethod", "LINEAR");                              // -> 0
    addScalar("rotationAxisCoordinates", "ROTATION_AXIS_IN_MODEL_COORDINATES"); // -> 0
    addScalar("isAdditive", "true");
    d.genericModifiers[tw.name] = tw;

    return d;
}

// Build a second BehaviorData that exercises the Tier-B kinds added with the
// transpiler class expansion: hkbModifierGenerator (root), BSOffsetAnimationGenerator,
// hkbModifierList. Root generator is a hkbModifierGenerator so the whole graph hangs
// off the behavior root and round-trips through the real BehaviorGraph path.
//
//   root MG_Root (hkbModifierGenerator)
//     modifier  -> ML_List (hkbModifierList) of [ Twist_A (hkbTwistModifier),
//                                                 Active_B (BSIsActiveModifier) ]
//     generator -> OAG_Offset (BSOffsetAnimationGenerator)
//                    default -> CLIP_Base, offsetClip -> CLIP_Off
BehaviorData makeData2() {
    BehaviorData d;
    d.behavior.behavior.name          = "TierB.hkb";
    d.behavior.behavior.variableMode  = "VARIABLE_MODE_DISCARD_WHEN_INACTIVE";  // -> 0
    d.behavior.behavior.rootGenerator = "MG_Root";
    d.behavior.behavior.data          = "graphdata";

    // ── graph data: 1 var (for a binding) + 1 event (for the EveryN/event kinds) ──
    BehaviorGraphDataDef gd;
    gd.variables.push_back({"Speed", "VARIABLE_TYPE_REAL", "ROLE_DEFAULT", 0, 0, std::nullopt});
    gd.events.push_back({"Fire", "0"});
    gd.events.push_back({"Reload", "0"});
    d.graphData = gd;

    // ── clips used by the offset animation generator ──
    ClipGeneratorDef cBase;
    cBase.name = "CLIP_Base";
    cBase.animationName = "Animations\\Base.hkx";
    cBase.playbackSpeed = "1.000000";
    d.clips[cBase.name] = cBase;

    ClipGeneratorDef cOff;
    cOff.name = "CLIP_Off";
    cOff.animationName = "Animations\\Off.hkx";
    cOff.playbackSpeed = "1.000000";
    d.clips[cOff.name] = cOff;

    // ── BSOffsetAnimationGenerator ──
    BSOffsetAnimationGeneratorDef oag;
    oag.name = "OAG_Offset";
    oag.userData = 7;
    oag.pDefaultGenerator    = "CLIP_Base";
    oag.pOffsetClipGenerator = "CLIP_Off";
    oag.fOffsetVariable   = "0.250000";
    oag.fOffsetRangeStart = "0.100000";
    oag.fOffsetRangeEnd   = "0.900000";
    d.offsetAnimGenerators[oag.name] = oag;

    // ── two modifiers for the modifier list ──
    GenericModifierDef tw;
    tw.className = "hkbTwistModifier";
    tw.name = "Twist_A";
    tw.userData = 3;
    tw.enable = true;
    auto addScalar = [&](const char* k, const char* v) {
        GenericParam p; p.name = k; p.kind = GenericParamKind::Scalar; p.scalarValue = v;
        tw.extraParams.push_back(p);
    };
    addScalar("axisOfRotation", "(0.000000 0.000000 1.000000 0.000000)");
    addScalar("startBoneIndex", "10");
    addScalar("endBoneIndex", "12");
    addScalar("isAdditive", "false");
    d.genericModifiers[tw.name] = tw;

    BSIsActiveModifierDef iam;
    iam.name = "Active_B";
    iam.userData = 4;
    iam.enable = true;
    iam.bIsActive0 = true;
    iam.bInvertActive2 = true;
    d.isActiveModifiers[iam.name] = iam;

    // ── hkbModifierList holding the two modifiers ──
    ModifierListDef ml;
    ml.name = "ML_List";
    ml.userData = 5;
    ml.enable = true;
    ml.modifiers = {"Twist_A", "Active_B"};
    d.modifierLists[ml.name] = ml;

    // ── hkbModifierGenerator root: modifier=ML_List, generator=OAG_Offset ──
    ModifierGeneratorDef mg;
    mg.name = "MG_Root";
    mg.userData = 9;
    mg.modifier  = "ML_List";
    mg.generator = "OAG_Offset";
    d.modifierGenerators[mg.name] = mg;

    return d;
}

} // namespace

void run_builder_tests() {
    BehaviorData data = makeData();

    try {
        BehaviorBuilder builder(data);
        std::shared_ptr<hkRootLevelContainer> root = builder.Build();
        CHECK(root != nullptr);
        if (!root) return;

        // Serialize -> bytes -> deserialize.
        PackFileSerializer ser;
        BinaryWriterEx bw;
        ser.Serialize(root, bw, HKXHeader::SkyrimSE());

        BinaryReaderEx br(bw.Data());
        PackFileDeserializer des;
        std::shared_ptr<IHavokObject> rt = des.Deserialize(br);
        CHECK(rt != nullptr);

        auto root2 = std::dynamic_pointer_cast<hkRootLevelContainer>(rt);
        CHECK(root2 != nullptr);
        if (!root2) return;
        CHECK(root2->m_namedVariants.size() == 1);
        CHECK(root2->m_namedVariants[0].m_className == "hkbBehaviorGraph");

        auto graph = std::dynamic_pointer_cast<hkbBehaviorGraph>(root2->m_namedVariants[0].m_variant);
        CHECK(graph != nullptr);
        if (!graph) return;
        CHECK(graph->m_name == "TestBehavior.hkb");
        CHECK(graph->m_variableMode == 1);  // MAINTAIN_VALUES_WHEN_INACTIVE

        // ── graph data ──
        auto gdata = graph->m_data;
        CHECK(gdata != nullptr);
        if (gdata) {
            CHECK(gdata->m_variableInfos.size() == 3);
            CHECK(gdata->m_eventInfos.size() == 3 - 1);  // 2 events
            // event flags: TF_HoverLand -> FLAG_SYNC_POINT (2)
            if (gdata->m_eventInfos.size() == 2)
                CHECK(gdata->m_eventInfos[1].m_flags == 2u);
            // variable types: BOOL(0), REAL(4), REAL(4)
            if (gdata->m_variableInfos.size() == 3) {
                CHECK(gdata->m_variableInfos[0].m_type == 0);
                CHECK(gdata->m_variableInfos[1].m_type == 4);
            }
            auto sdata = gdata->m_stringData;
            CHECK(sdata != nullptr);
            if (sdata) {
                CHECK(sdata->m_variableNames.size() == 3);
                CHECK(sdata->m_variableNames[0] == "TF_IsFlying");
                CHECK(sdata->m_eventNames.size() == 2);
                CHECK(sdata->m_eventNames[0] == "TF_FlightStart");
            }
            auto vvs = gdata->m_variableInitialValues;
            CHECK(vvs != nullptr);
            if (vvs) CHECK(vvs->m_wordVariableValues.size() == 3);
        }

        // ── root SM ──
        auto sm = std::dynamic_pointer_cast<hkbStateMachine>(graph->m_rootGenerator);
        CHECK(sm != nullptr);
        if (!sm) return;
        CHECK(sm->m_name == "TrueFlightSM");
        CHECK(sm->m_startStateId == 0);
        CHECK(sm->m_states.size() == 2);

        // SM binding: isActive -> TF_IsFlying (variable index 0)
        CHECK(sm->m_variableBindingSet != nullptr);
        if (sm->m_variableBindingSet) {
            CHECK(sm->m_variableBindingSet->m_bindings.size() == 1);
            if (!sm->m_variableBindingSet->m_bindings.empty()) {
                auto& b = sm->m_variableBindingSet->m_bindings[0];
                CHECK(b.m_memberPath == "isActive");
                CHECK(b.m_variableIndex == 0);  // TF_IsFlying
            }
        }

        // SM wildcard transition: event TF_FlightStart (id 0), toState id 1,
        // flags FLAG_IS_LOCAL_WILDCARD|FLAG_DISABLE_CONDITION = 2304.
        CHECK(sm->m_wildcardTransitions != nullptr);
        if (sm->m_wildcardTransitions) {
            CHECK(sm->m_wildcardTransitions->m_transitions.size() == 1);
            if (!sm->m_wildcardTransitions->m_transitions.empty()) {
                auto& t = sm->m_wildcardTransitions->m_transitions[0];
                CHECK(t.m_eventId == 0);     // TF_FlightStart
                CHECK(t.m_toStateId == 1);   // ST_Flight
                CHECK(t.m_flags == static_cast<std::int16_t>(2304));
                CHECK(t.m_transition != nullptr);
                auto eff = std::dynamic_pointer_cast<hkbBlendingTransitionEffect>(t.m_transition);
                CHECK(eff != nullptr);
                if (eff) {
                    CHECK(eff->m_name == "FlightBlend");
                    CHECK(eff->m_duration == 0.3f);
                }
            }
        }

        // ── state ST_Hover (states[0]) ──
        auto st0 = sm->m_states.size() > 0 ? sm->m_states[0] : nullptr;
        CHECK(st0 != nullptr);
        if (st0) {
            CHECK(st0->m_name == "ST_Hover");
            CHECK(st0->m_stateId == 0);
            // enter notify event TF_FlightStart (id 0)
            CHECK(st0->m_enterNotifyEvents != nullptr);
            if (st0->m_enterNotifyEvents) {
                CHECK(st0->m_enterNotifyEvents->m_events.size() == 1);
                if (!st0->m_enterNotifyEvents->m_events.empty())
                    CHECK(st0->m_enterNotifyEvents->m_events[0].m_id == 0);
            }
            // generator chain: cyclic -> blender -> clip children
            auto cyc = std::dynamic_pointer_cast<BSCyclicBlendTransitionGenerator>(st0->m_generator);
            CHECK(cyc != nullptr);
            if (cyc) {
                CHECK(cyc->m_name == "TF_DirectionCyclic");
                CHECK(cyc->m_fTransitionDuration == 0.2f);
                // freeze event resolved to TF_HoverLand (id 1)
                CHECK(cyc->m_EventToFreezeBlendValue.m_id == 1);
                // binding fBlendParameter -> TF_HoverDirection (index 1)
                CHECK(cyc->m_variableBindingSet != nullptr);
                if (cyc->m_variableBindingSet && !cyc->m_variableBindingSet->m_bindings.empty())
                    CHECK(cyc->m_variableBindingSet->m_bindings[0].m_variableIndex == 1);

                auto blend = std::dynamic_pointer_cast<hkbBlenderGenerator>(cyc->m_pBlenderGenerator);
                CHECK(blend != nullptr);
                if (blend) {
                    CHECK(blend->m_name == "TF_Direction_Blend");
                    CHECK(blend->m_flags == 49);
                    CHECK(blend->m_children.size() == 2);
                    if (blend->m_children.size() == 2) {
                        auto child0 = blend->m_children[0];
                        CHECK(child0 != nullptr);
                        if (child0) {
                            // child0 bone weights resolved against 4-bone skeleton:
                            // index 2 (Spine) = 1, index 3 (Head) = 1, others 0.
                            CHECK(child0->m_boneWeights != nullptr);
                            if (child0->m_boneWeights) {
                                CHECK(child0->m_boneWeights->m_boneWeights.size() == 4);
                                if (child0->m_boneWeights->m_boneWeights.size() == 4) {
                                    CHECK(child0->m_boneWeights->m_boneWeights[0] == 0.f);
                                    CHECK(child0->m_boneWeights->m_boneWeights[2] == 1.f);
                                    CHECK(child0->m_boneWeights->m_boneWeights[3] == 1.f);
                                }
                            }
                            auto clip0 = std::dynamic_pointer_cast<hkbClipGenerator>(child0->m_generator);
                            CHECK(clip0 != nullptr);
                            if (clip0) {
                                CHECK(clip0->m_animationName == "Animations\\Fwd.hkx");
                                CHECK(clip0->m_mode == 1);  // MODE_LOOPING
                                CHECK(clip0->m_playbackSpeed == 1.25f);
                                // trigger -> event id 1 (TF_HoverLand), relativeToEndOfClip
                                CHECK(clip0->m_triggers != nullptr);
                                if (clip0->m_triggers) {
                                    CHECK(clip0->m_triggers->m_triggers.size() == 1);
                                    if (!clip0->m_triggers->m_triggers.empty()) {
                                        auto& tr = clip0->m_triggers->m_triggers[0];
                                        CHECK(tr.m_event.m_id == 1);
                                        CHECK(tr.m_relativeToEndOfClip == true);
                                        auto pl = std::dynamic_pointer_cast<hkbStringEventPayload>(tr.m_event.m_payload);
                                        CHECK(pl != nullptr);
                                        if (pl) CHECK(pl->m_data == "PayloadA");
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        // ── generic hkbTwistModifier via the builder's own node path ──
        // The twist modifier has no generator slot in the M1 set, so it cannot hang
        // off the behavior root through a (stubbed) hkbModifierGenerator. Exercise
        // the builder's generic-modifier mapping directly via BuildNodeByName, then
        // round-trip it as a standalone packfile object.
        {
            BehaviorBuilder b2(data);
            auto node = b2.BuildNodeByName("TFA_BowSpineTwist");
            CHECK(node != nullptr);
            auto tw = std::dynamic_pointer_cast<hkbTwistModifier>(node);
            CHECK(tw != nullptr);
            if (tw) {
                CHECK(tw->m_name == "TFA_BowSpineTwist");
                CHECK(tw->m_userData == 1u);
                CHECK(tw->m_enable == true);
                CHECK(tw->m_startBoneIndex == 24);
                CHECK(tw->m_endBoneIndex == 26);
                CHECK(tw->m_setAngleMethod == 0);            // LINEAR
                CHECK(tw->m_rotationAxisCoordinates == 0);   // IN_MODEL_COORDINATES
                CHECK(tw->m_isAdditive == true);
                CHECK(tw->m_axisOfRotation == (Vector4{1.f, 0.f, 0.f, 0.f}));
                // binding twistAngle -> Pitch (variable index 2)
                CHECK(tw->m_variableBindingSet != nullptr);
                if (tw->m_variableBindingSet && !tw->m_variableBindingSet->m_bindings.empty())
                    CHECK(tw->m_variableBindingSet->m_bindings[0].m_variableIndex == 2);

                // standalone round-trip of the built object.
                auto rootc = std::make_shared<hkRootLevelContainer>();
                hkRootLevelContainerNamedVariant nv;
                nv.m_name = "hkbTwistModifier"; nv.m_className = "hkbTwistModifier"; nv.m_variant = tw;
                rootc->m_namedVariants.push_back(nv);
                PackFileSerializer ser2; BinaryWriterEx bw2;
                ser2.Serialize(rootc, bw2, HKXHeader::SkyrimSE());
                BinaryReaderEx br2(bw2.Data());
                PackFileDeserializer des2;
                auto rt2 = std::dynamic_pointer_cast<hkRootLevelContainer>(des2.Deserialize(br2));
                CHECK(rt2 != nullptr);
                if (rt2 && rt2->m_namedVariants.size() == 1) {
                    auto tw2 = std::dynamic_pointer_cast<hkbTwistModifier>(rt2->m_namedVariants[0].m_variant);
                    CHECK(tw2 != nullptr);
                    if (tw2) {
                        CHECK(tw2->m_startBoneIndex == 24);
                        CHECK(tw2->m_isAdditive == true);
                        CHECK(tw2->m_axisOfRotation == (Vector4{1.f, 0.f, 0.f, 0.f}));
                    }
                }
            }
        }

        // ── Tier-B kinds: hkbModifierGenerator root -> { hkbModifierList,
        //    BSOffsetAnimationGenerator }. Full BehaviorGraph round-trip. ──
        {
            BehaviorData data2 = makeData2();
            BehaviorBuilder builder2(data2);
            std::shared_ptr<hkRootLevelContainer> rootB = builder2.Build();
            CHECK(rootB != nullptr);
            if (rootB) {
                PackFileSerializer serB; BinaryWriterEx bwB;
                serB.Serialize(rootB, bwB, HKXHeader::SkyrimSE());
                BinaryReaderEx brB(bwB.Data());
                PackFileDeserializer desB;
                auto rootB2 = std::dynamic_pointer_cast<hkRootLevelContainer>(desB.Deserialize(brB));
                CHECK(rootB2 != nullptr);
                if (rootB2 && rootB2->m_namedVariants.size() == 1) {
                    auto graphB = std::dynamic_pointer_cast<hkbBehaviorGraph>(rootB2->m_namedVariants[0].m_variant);
                    CHECK(graphB != nullptr);
                    if (graphB) {
                        CHECK(graphB->m_name == "TierB.hkb");
                        CHECK(graphB->m_variableMode == 0);  // DISCARD_WHEN_INACTIVE

                        // root = hkbModifierGenerator
                        auto mg = std::dynamic_pointer_cast<hkbModifierGenerator>(graphB->m_rootGenerator);
                        CHECK(mg != nullptr);
                        if (mg) {
                            CHECK(mg->m_name == "MG_Root");
                            CHECK(mg->m_userData == 9u);

                            // mg.modifier = hkbModifierList with 2 children
                            auto ml = std::dynamic_pointer_cast<hkbModifierList>(mg->m_modifier);
                            CHECK(ml != nullptr);
                            if (ml) {
                                CHECK(ml->m_name == "ML_List");
                                CHECK(ml->m_enable == true);
                                CHECK(ml->m_modifiers.size() == 2);
                                if (ml->m_modifiers.size() == 2) {
                                    auto twA = std::dynamic_pointer_cast<hkbTwistModifier>(ml->m_modifiers[0]);
                                    CHECK(twA != nullptr);
                                    if (twA) {
                                        CHECK(twA->m_name == "Twist_A");
                                        CHECK(twA->m_startBoneIndex == 10);
                                        CHECK(twA->m_endBoneIndex == 12);
                                        CHECK(twA->m_axisOfRotation == (Vector4{0.f, 0.f, 1.f, 0.f}));
                                    }
                                    auto actB = std::dynamic_pointer_cast<BSIsActiveModifier>(ml->m_modifiers[1]);
                                    CHECK(actB != nullptr);
                                    if (actB) {
                                        CHECK(actB->m_name == "Active_B");
                                        CHECK(actB->m_bIsActive0 == true);
                                        CHECK(actB->m_bInvertActive2 == true);
                                        CHECK(actB->m_bIsActive1 == false);
                                    }
                                }
                            }

                            // mg.generator = BSOffsetAnimationGenerator
                            auto oag = std::dynamic_pointer_cast<BSOffsetAnimationGenerator>(mg->m_generator);
                            CHECK(oag != nullptr);
                            if (oag) {
                                CHECK(oag->m_name == "OAG_Offset");
                                CHECK(oag->m_fOffsetVariable == 0.25f);
                                CHECK(oag->m_fOffsetRangeStart == 0.1f);
                                CHECK(oag->m_fOffsetRangeEnd == 0.9f);
                                auto base = std::dynamic_pointer_cast<hkbClipGenerator>(oag->m_pDefaultGenerator);
                                CHECK(base != nullptr);
                                if (base) CHECK(base->m_animationName == "Animations\\Base.hkx");
                                auto off = std::dynamic_pointer_cast<hkbClipGenerator>(oag->m_pOffsetClipGenerator);
                                CHECK(off != nullptr);
                                if (off) CHECK(off->m_animationName == "Animations\\Off.hkx");
                            }
                        }
                    }
                }
            }
        }

        // ── Tier-B leaf builders exercised directly via BuildNodeByName (kinds
        //    with no generator slot to hang off the graph root). ──
        {
            BehaviorData data2 = makeData2();
            BehaviorBuilder b3(data2);
            // hkbModifierList built standalone (≥2 modifiers, child wiring preserved).
            auto node = b3.BuildNodeByName("ML_List");
            auto ml = std::dynamic_pointer_cast<hkbModifierList>(node);
            CHECK(ml != nullptr);
            if (ml) {
                CHECK(ml->m_modifiers.size() == 2);
                // memoization: the same modifier object is reused across references.
                auto twDirect = std::dynamic_pointer_cast<hkbTwistModifier>(b3.BuildNodeByName("Twist_A"));
                CHECK(twDirect != nullptr);
                if (twDirect && ml->m_modifiers.size() == 2)
                    CHECK(ml->m_modifiers[0].get() == twDirect.get());
            }
        }

    } catch (const std::exception& e) {
        std::printf("  builder round-trip threw: %s\n", e.what());
        CHECK(false);
    }
}
