// havok-core — M2: serializer smoke test. Builds a real graph (SM -> State ->
// Clip), serializes it, and validates the packfile is structurally well-formed:
// the SSE magic, the three sections, the concrete class names in __classnames__,
// and the authored strings in __data__ (proving the virtual-fixup + local-write
// queues both ran). Byte-exactness vs the .NET/HkxErWriter oracle is the M2 exit
// check, done in the full VS environment.

#include "test_harness.h"

#include "havok/classes/Classes.h"
#include "havok/core/PackFileSerializer.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

using namespace havok;

static bool contains(const std::vector<std::uint8_t>& buf, const std::string& needle) {
    if (needle.empty() || needle.size() > buf.size()) return false;
    auto it = std::search(buf.begin(), buf.end(), needle.begin(), needle.end());
    return it != buf.end();
}

void run_packfile_tests() {
    // SM -> StateInfo -> ClipGenerator
    auto clip = std::make_shared<hkbClipGenerator>();
    clip->m_animationName = "FlightForward";
    clip->m_playbackSpeed = 1.0f;
    auto info = std::make_shared<hkbStateMachineStateInfo>();
    info->m_name = "FlightState";
    info->m_stateId = 1;
    info->m_generator = clip;
    auto sm = std::make_shared<hkbStateMachine>();
    sm->m_startStateId = 1;
    sm->m_states.push_back(info);

    PackFileSerializer ser;
    BinaryWriterEx bw;
    ser.Serialize(sm, bw, HKXHeader::SkyrimSE());
    const std::vector<std::uint8_t>& out = bw.Data();

    // a real, multi-section packfile came out
    CHECK(out.size() > 256);

    // SSE magic (both magics are byte-palindromes, so endianness-independent)
    const std::uint8_t magic[8] = {0x57, 0xE0, 0xE0, 0x57, 0x10, 0xC0, 0xC0, 0x10};
    bool magicOk = out.size() >= 8;
    for (int i = 0; i < 8 && magicOk; ++i) magicOk = (out[i] == magic[i]);
    CHECK(magicOk);

    // version string is in the header
    CHECK(contains(out, "hk_2010.2.0-r1"));

    // three sections framed
    CHECK(contains(out, "__classnames__"));
    CHECK(contains(out, "__types__"));
    CHECK(contains(out, "__data__"));

    // every serialized object got a class-name entry (virtual-fixup path ran)
    CHECK(contains(out, "hkbStateMachine"));
    CHECK(contains(out, "hkbStateMachineStateInfo"));
    CHECK(contains(out, "hkbClipGenerator"));
    // the always-present Havok class names are present too
    CHECK(contains(out, "hkClass"));

    // authored strings were emitted by the local-write queue
    CHECK(contains(out, "FlightForward"));
    CHECK(contains(out, "FlightState"));
}
