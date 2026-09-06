// havok-core — M1: Tier-A class-model tests (base inheritance chain).
// Validates the POCO model compiles, instantiates, exposes the right fields
// across the chain, reports correct Signature/ClassName, and behaves
// polymorphically through a shared_ptr<IHavokObject>.

#include "test_harness.h"

#include "havok/classes/Classes.h"
#include "havok/classes/IHavokObject.h"

#include <memory>
#include <string>
#include <type_traits>

using namespace havok;

void run_classes_tests() {
    // ── inheritance chain (compile-time) ──────────────────────────────────────
    static_assert(std::is_base_of_v<IHavokObject, hkBaseObject>);
    static_assert(std::is_base_of_v<hkBaseObject, hkReferencedObject>);
    static_assert(std::is_base_of_v<hkReferencedObject, hkbBindable>);
    static_assert(std::is_base_of_v<hkbBindable, hkbNode>);
    static_assert(std::is_base_of_v<hkbNode, hkbGenerator>);

    // ── class signatures (CRCs from the HKX2E headers) ────────────────────────
    CHECK(hkBaseObject{}.Signature() == 0xe0708a00u);
    CHECK(hkReferencedObject{}.Signature() == 0x3b1c1113u);
    CHECK(hkbBindable{}.Signature() == 0x2c1432d7u);
    CHECK(hkbNode{}.Signature() == 0x6d26f61du);
    CHECK(hkbGenerator{}.Signature() == 0x0d68aefcu);

    // ── most-derived identity on a concrete instance ──────────────────────────
    hkbGenerator g;
    CHECK(g.Signature() == 0x0d68aefcu);
    CHECK(std::string(g.ClassName()) == "hkbGenerator");

    // ── fields from every level are reachable on the derived instance ─────────
    g.m_referenceCount = 1;        // hkReferencedObject
    g.m_areBindablesCached = true; // hkbBindable
    g.m_userData = 0xCAFEull;      // hkbNode
    g.m_name = "TestGen";          // hkbNode
    CHECK(g.m_referenceCount == 1);
    CHECK(g.m_areBindablesCached == true);
    CHECK(g.m_userData == 0xCAFEull);
    CHECK(g.m_name == "TestGen");
    CHECK(g.m_variableBindingSet == nullptr);  // pointer field defaults to null

    // ── polymorphism through the base interface + shared_ptr graph ────────────
    std::shared_ptr<IHavokObject> obj = std::make_shared<hkbGenerator>();
    CHECK(obj->Signature() == 0x0d68aefcu);
    CHECK(std::string(obj->ClassName()) == "hkbGenerator");

    auto node = std::dynamic_pointer_cast<hkbNode>(obj);
    CHECK(node != nullptr);
    if (node) {
        node->m_name = "viaBase";
        CHECK(node->m_name == "viaBase");
    }
    // base slice should NOT cast to an unrelated/more-derived sibling chain
    CHECK(std::dynamic_pointer_cast<hkbGenerator>(obj) != nullptr);

    // ── build a small behavior fragment: SM -> StateInfo -> ClipGenerator ──────
    auto sm   = std::make_shared<hkbStateMachine>();
    auto info = std::make_shared<hkbStateMachineStateInfo>();
    auto clip = std::make_shared<hkbClipGenerator>();

    clip->m_animationName = "HoverIdle";
    clip->m_playbackSpeed = 1.0f;
    clip->m_mode = 1;  // PlaybackMode (sbyte-backed)

    info->m_name = "Hover";
    info->m_stateId = 0;
    info->m_enable = true;
    info->m_generator = clip;  // shared_ptr<hkbGenerator> <- shared_ptr<hkbClipGenerator>

    sm->m_startStateId = 0;
    sm->m_states.push_back(info);
    sm->m_eventToSendWhenStateOrTransitionChanges.m_id = -1;  // inline hkbEvent field

    CHECK(sm->Signature() == 0x816c1dcbu);
    CHECK(info->Signature() == 0x0ed7f9d0u);
    CHECK(clip->Signature() == 0x333b85b9u);
    CHECK(std::string(clip->ClassName()) == "hkbClipGenerator");

    CHECK(sm->m_states.size() == 1);
    CHECK(sm->m_states[0]->m_name == "Hover");
    CHECK(sm->m_states[0]->m_enable == true);
    CHECK(sm->m_states[0]->m_generator != nullptr);
    CHECK(std::string(sm->m_states[0]->m_generator->ClassName()) == "hkbClipGenerator");

    auto clipBack = std::dynamic_pointer_cast<hkbClipGenerator>(sm->m_states[0]->m_generator);
    CHECK(clipBack != nullptr);
    CHECK(clipBack && clipBack->m_animationName == "HoverIdle");
    CHECK(clipBack && clipBack->m_playbackSpeed == 1.0f);

    // the inline event member carries its own identity + fields
    CHECK(sm->m_eventToSendWhenStateOrTransitionChanges.m_id == -1);
    CHECK(sm->m_eventToSendWhenStateOrTransitionChanges.Signature() == 0x3e0fd810u);

    // ── full top-level graph closure: container -> graph -> data/strings ->
    //    SM(root) -> state -> generators/effects/modifiers ───────────────────────
    auto root  = std::make_shared<hkRootLevelContainer>();
    auto graph = std::make_shared<hkbBehaviorGraph>();
    auto data  = std::make_shared<hkbBehaviorGraphData>();
    auto strs  = std::make_shared<hkbBehaviorGraphStringData>();
    auto vvs   = std::make_shared<hkbVariableValueSet>();

    strs->m_variableNames = {"TF_IsFlying", "TF_HoverSpeed"};
    strs->m_eventNames    = {"TF_FlightStart", "TF_FlightStop"};
    data->m_stringData            = strs;
    data->m_variableInitialValues = vvs;
    data->m_variableInfos.push_back(hkbVariableInfo{});
    data->m_eventInfos.push_back(hkbEventInfo{});
    vvs->m_wordVariableValues.push_back(hkbVariableValue{});

    // generators: a clip inside a blender, a behavior-reference BFR, a cyclic blend
    auto flightClip = std::make_shared<hkbClipGenerator>();
    flightClip->m_animationName = "FlightForward";
    auto child = std::make_shared<hkbBlenderGeneratorChild>();
    child->m_generator = flightClip;
    child->m_weight = 1.0f;
    auto blender = std::make_shared<hkbBlenderGenerator>();
    blender->m_children.push_back(child);

    auto bfr = std::make_shared<hkbBehaviorReferenceGenerator>();
    bfr->m_behaviorName = "Behaviors\\TrueFlight.hkx";

    auto cyclic = std::make_shared<BSCyclicBlendTransitionGenerator>();
    cyclic->m_pBlenderGenerator = blender;
    cyclic->m_EventToCrossBlend.m_id = 7;  // inline hkbEventProperty

    // a state hosting the cyclic generator, with a wildcard transition array
    auto stateInfo = std::make_shared<hkbStateMachineStateInfo>();
    stateInfo->m_name = "Flight";
    stateInfo->m_stateId = 1;
    stateInfo->m_generator = cyclic;

    auto tia = std::make_shared<hkbStateMachineTransitionInfoArray>();
    {
        hkbStateMachineTransitionInfo t;
        t.m_eventId   = 0;
        t.m_toStateId = 1;
        t.m_flags     = 0x0400;  // (global wildcard flag bit)
        tia->m_transitions.push_back(t);
    }
    stateInfo->m_transitions = tia;

    auto rootSM = std::make_shared<hkbStateMachine>();
    rootSM->m_states.push_back(stateInfo);
    rootSM->m_startStateId = 1;

    graph->m_rootGenerator = rootSM;
    graph->m_data = data;

    hkRootLevelContainerNamedVariant named;
    named.m_name = "hkbBehaviorGraph";
    named.m_className = "hkbBehaviorGraph";
    named.m_variant = graph;  // shared_ptr<hkReferencedObject> <- hkbBehaviorGraph
    root->m_namedVariants.push_back(named);

    // signatures across the new closure
    CHECK(root->Signature() == 0x2772c11eu);
    CHECK(graph->Signature() == 0xb1218f86u);
    CHECK(data->Signature() == 0x095aca5du);
    CHECK(strs->Signature() == 0xc713064eu);
    CHECK(vvs->Signature() == 0x27812d8du);
    CHECK(blender->Signature() == 0x22df7147u);
    CHECK(child->Signature() == 0xe2b384b0u);
    CHECK(bfr->Signature() == 0x0fcb5423u);
    CHECK(cyclic->Signature() == 0x5119eb06u);
    CHECK(tia->Signature() == 0xe397b11eu);

    // the graph wires together and is walkable by pointer
    CHECK(root->m_namedVariants.size() == 1);
    CHECK(root->m_namedVariants[0].m_variant == graph);
    CHECK(graph->m_rootGenerator == rootSM);
    CHECK(graph->m_data->m_stringData->m_variableNames.size() == 2);
    CHECK(graph->m_data->m_stringData->m_variableNames[0] == "TF_IsFlying");
    CHECK(std::string(rootSM->m_states[0]->m_generator->ClassName()) ==
          "BSCyclicBlendTransitionGenerator");

    auto cyc = std::dynamic_pointer_cast<BSCyclicBlendTransitionGenerator>(
        rootSM->m_states[0]->m_generator);
    CHECK(cyc != nullptr);
    CHECK(cyc && cyc->m_EventToCrossBlend.m_id == 7);
    auto bl = cyc ? std::dynamic_pointer_cast<hkbBlenderGenerator>(cyc->m_pBlenderGenerator)
                  : nullptr;
    CHECK(bl != nullptr);
    CHECK(bl && bl->m_children.size() == 1);
    auto cg = bl ? std::dynamic_pointer_cast<hkbClipGenerator>(bl->m_children[0]->m_generator)
                 : nullptr;
    CHECK(cg != nullptr);
    CHECK(cg && cg->m_animationName == "FlightForward");
    CHECK(stateInfo->m_transitions->m_transitions.size() == 1);
    CHECK(stateInfo->m_transitions->m_transitions[0].m_toStateId == 1);

    // a modifier + a string payload, for closure coverage
    auto twist = std::make_shared<hkbTwistModifier>();
    twist->m_twistAngle = 1.5f;
    twist->m_isAdditive = true;
    CHECK(twist->Signature() == 0xb6b76b32u);
    CHECK(twist->m_enable == false);  // inherited hkbModifier field

    auto payload = std::make_shared<hkbStringEventPayload>();
    payload->m_data = "hello";
    CHECK(payload->Signature() == 0xed04256au);
    CHECK(std::dynamic_pointer_cast<hkbEventPayload>(payload) != nullptr);
}
