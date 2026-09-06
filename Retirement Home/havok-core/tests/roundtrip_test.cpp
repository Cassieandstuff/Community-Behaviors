// havok-core — M3: standalone round-trip. Build a graph, serialize (M2),
// deserialize (M3), and confirm the reconstructed model equals the original.
// Self-verifying: Write and Read are independently derived, so a mutual layout
// disagreement fails here without needing any external oracle.

#include "test_harness.h"

#include "havok/classes/Classes.h"
#include "havok/core/PackFileDeserializer.h"
#include "havok/core/PackFileSerializer.h"

#include <exception>
#include <memory>
#include <string>

using namespace havok;

void run_roundtrip_tests() {
    // container -> behaviorGraph(+data+strings) -> SM -> state -> clip
    auto clip = std::make_shared<hkbClipGenerator>();
    clip->m_animationName = "FlightForward";
    clip->m_playbackSpeed = 1.5f;
    clip->m_startTime = 0.25f;
    clip->m_mode = 2;

    auto info = std::make_shared<hkbStateMachineStateInfo>();
    info->m_name = "FlightState";
    info->m_stateId = 7;
    info->m_probability = 0.5f;
    info->m_enable = true;
    info->m_generator = clip;

    auto sm = std::make_shared<hkbStateMachine>();
    sm->m_name = "TrueFlightSM";
    sm->m_startStateId = 7;
    sm->m_states.push_back(info);
    sm->m_eventToSendWhenStateOrTransitionChanges.m_id = -1;

    auto strs = std::make_shared<hkbBehaviorGraphStringData>();
    strs->m_variableNames = {"TF_IsFlying", "TF_HoverSpeed"};
    strs->m_eventNames = {"TF_FlightStart"};
    auto data = std::make_shared<hkbBehaviorGraphData>();
    data->m_stringData = strs;

    auto graph = std::make_shared<hkbBehaviorGraph>();
    graph->m_name = "TrueFlight.hkb";
    graph->m_variableMode = 1;
    graph->m_rootGenerator = sm;
    graph->m_data = data;

    auto root = std::make_shared<hkRootLevelContainer>();
    hkRootLevelContainerNamedVariant nv;
    nv.m_name = "hkbBehaviorGraph";
    nv.m_className = "hkbBehaviorGraph";
    nv.m_variant = graph;
    root->m_namedVariants.push_back(nv);

    try {
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
        CHECK(root2->Signature() == 0x2772c11eu);
        CHECK(root2->m_namedVariants.size() == 1);
        CHECK(root2->m_namedVariants[0].m_name == "hkbBehaviorGraph");
        CHECK(root2->m_namedVariants[0].m_className == "hkbBehaviorGraph");

        auto graph2 = std::dynamic_pointer_cast<hkbBehaviorGraph>(root2->m_namedVariants[0].m_variant);
        CHECK(graph2 != nullptr);
        if (!graph2) return;
        CHECK(graph2->m_name == "TrueFlight.hkb");
        CHECK(graph2->m_variableMode == 1);

        auto data2 = graph2->m_data;
        CHECK(data2 != nullptr);
        if (data2 && data2->m_stringData) {
            CHECK(data2->m_stringData->m_variableNames.size() == 2);
            CHECK(data2->m_stringData->m_variableNames[0] == "TF_IsFlying");
            CHECK(data2->m_stringData->m_variableNames[1] == "TF_HoverSpeed");
            CHECK(data2->m_stringData->m_eventNames.size() == 1);
            CHECK(data2->m_stringData->m_eventNames[0] == "TF_FlightStart");
        } else {
            CHECK(false);  // string data must survive
        }

        auto sm2 = std::dynamic_pointer_cast<hkbStateMachine>(graph2->m_rootGenerator);
        CHECK(sm2 != nullptr);
        if (!sm2) return;
        CHECK(sm2->m_name == "TrueFlightSM");
        CHECK(sm2->m_startStateId == 7);
        CHECK(sm2->m_eventToSendWhenStateOrTransitionChanges.m_id == -1);
        CHECK(sm2->m_states.size() == 1);
        if (sm2->m_states.size() == 1) {
            auto& info2 = sm2->m_states[0];
            CHECK(info2 != nullptr);
            CHECK(info2->m_name == "FlightState");
            CHECK(info2->m_stateId == 7);
            CHECK(info2->m_probability == 0.5f);
            CHECK(info2->m_enable == true);
            auto clip2 = std::dynamic_pointer_cast<hkbClipGenerator>(info2->m_generator);
            CHECK(clip2 != nullptr);
            if (clip2) {
                CHECK(clip2->m_animationName == "FlightForward");
                CHECK(clip2->m_playbackSpeed == 1.5f);
                CHECK(clip2->m_startTime == 0.25f);
                CHECK(clip2->m_mode == 2);
            }
        }
    } catch (const std::exception& e) {
        std::printf("  round-trip threw: %s\n", e.what());
        CHECK(false);
    }
}
