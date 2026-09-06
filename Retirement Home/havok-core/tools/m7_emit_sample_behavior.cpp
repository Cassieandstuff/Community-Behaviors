// m7_emit_sample_behavior.cpp — M7 in-game proof harness (host tool, not shipped).
//
// Emits a minimal *net-new* sub-behavior packfile (.hkx) with havok-core and
// round-trip-validates it. Unlike the M2 oracle (which rebuilds Engine Relay's
// switchboard), this proves havok-core can author an ARBITRARY behavior graph
// from scratch — the capability SCT/blueprints rely on.
//
// The emitted graph (a behavior graph → state machine → one looping clip, plus
// an entry event + active variable) is the minimal shape Engine Relay can serve
// via RegisterSubBehavior. Hand it to ER and drive the in-game proof per
// docs/havok-core-m7-ingame-proof.md.
//
// Build (standalone, no CMake):
//   cl /nologo /std:c++latest /EHsc /W4 /wd4100 ^
//     /I "<repo>\libs\havok-core\include" ^
//     "<repo>\libs\havok-core\src\core\BinaryWriterEx.cpp" ^
//     "<repo>\libs\havok-core\src\core\BinaryReaderEx.cpp" ^
//     "<repo>\libs\havok-core\src\classes\ClassWrite.cpp" ^
//     "<repo>\libs\havok-core\src\classes\ClassRead.cpp" ^
//     m7_emit_sample_behavior.cpp /Fe:m7_emit.exe
//   m7_emit.exe [outPath.hkx]

#include "havok/classes/Classes.h"
#include "havok/core/PackFileDeserializer.h"
#include "havok/core/PackFileSerializer.h"

#include <cstdio>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

using namespace havok;

namespace {

constexpr const char* kGraphName = "HavokCoreSample.hkb";
constexpr const char* kAnimPath  = "Animations\\HavokCoreSample\\Sample.hkx";
constexpr const char* kEnterEvent = "HavokCore_Enter";
constexpr const char* kActiveVar  = "HavokCore_Active";

std::shared_ptr<hkRootLevelContainer> BuildSampleBehavior() {
    auto clip = std::make_shared<hkbClipGenerator>();
    clip->m_name                  = "Sample_Clip";
    clip->m_animationName         = kAnimPath;
    clip->m_playbackSpeed         = 1.0f;
    clip->m_animationBindingIndex = -1;
    clip->m_mode                  = 1;  // MODE_LOOPING

    auto state = std::make_shared<hkbStateMachineStateInfo>();
    state->m_name        = "Sample_State";
    state->m_stateId     = 0;
    state->m_probability = 1.0f;
    state->m_enable      = true;
    state->m_generator   = clip;

    auto sm = std::make_shared<hkbStateMachine>();
    sm->m_name                               = "Sample_SM";
    sm->m_eventToSendWhenStateOrTransitionChanges.m_id = -1;
    sm->m_returnToPreviousStateEventId       = -1;
    sm->m_randomTransitionEventId            = -1;
    sm->m_transitionToNextHigherStateEventId = -1;
    sm->m_transitionToNextLowerStateEventId  = -1;
    sm->m_syncVariableIndex                  = -1;
    sm->m_maxSimultaneousTransitions         = 32;
    sm->m_states.push_back(state);

    auto strs = std::make_shared<hkbBehaviorGraphStringData>();
    strs->m_eventNames    = {kEnterEvent};
    strs->m_variableNames = {kActiveVar};

    auto data = std::make_shared<hkbBehaviorGraphData>();
    data->m_eventInfos.push_back(hkbEventInfo{});      // one event
    data->m_variableInfos.push_back(hkbVariableInfo{}); // one variable
    data->m_variableInitialValues = std::make_shared<hkbVariableValueSet>();
    data->m_stringData            = strs;

    auto bg = std::make_shared<hkbBehaviorGraph>();
    bg->m_name          = kGraphName;
    bg->m_variableMode  = 0;
    bg->m_rootGenerator = sm;
    bg->m_data          = data;

    auto root = std::make_shared<hkRootLevelContainer>();
    hkRootLevelContainerNamedVariant nv;
    nv.m_name      = "hkbBehaviorGraph";
    nv.m_className = "hkbBehaviorGraph";
    nv.m_variant   = bg;
    root->m_namedVariants.push_back(nv);
    return root;
}

bool RoundTripOk(const std::vector<std::uint8_t>& bytes) {
    BinaryReaderEx br(bytes);
    PackFileDeserializer des;
    auto root = std::dynamic_pointer_cast<hkRootLevelContainer>(des.Deserialize(br));
    if (!root || root->m_namedVariants.size() != 1) return false;
    auto bg = std::dynamic_pointer_cast<hkbBehaviorGraph>(root->m_namedVariants[0].m_variant);
    if (!bg || bg->m_name != kGraphName) return false;
    auto sm = std::dynamic_pointer_cast<hkbStateMachine>(bg->m_rootGenerator);
    if (!sm || sm->m_states.size() != 1) return false;
    auto clip = std::dynamic_pointer_cast<hkbClipGenerator>(sm->m_states[0]->m_generator);
    return clip && clip->m_animationName == kAnimPath;
}

}  // namespace

int main(int argc, char** argv) {
    const std::string outPath = argc > 1 ? argv[1] : "HavokCoreSample.hkx";

    auto root = BuildSampleBehavior();
    PackFileSerializer ser;
    BinaryWriterEx bw;
    ser.Serialize(root, bw, HKXHeader::SkyrimSE());
    const auto bytes = bw.Data();

    if (!RoundTripOk(bytes)) {
        std::printf("ERROR: emitted behavior failed havok-core round-trip self-check\n");
        return 1;
    }

    std::ofstream f(outPath, std::ios::binary | std::ios::trunc);
    if (!f) { std::printf("ERROR: cannot open %s for writing\n", outPath.c_str()); return 1; }
    f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!f.good()) { std::printf("ERROR: write failed\n"); return 1; }

    std::printf("OK: wrote %zu bytes to %s\n", bytes.size(), outPath.c_str());
    std::printf("    graph='%s' event='%s' var='%s' anim='%s'\n",
                kGraphName, kEnterEvent, kActiveVar, kAnimPath);
    std::printf("    (round-trip self-check passed)\n");
    return 0;
}
