// havok-core — SCT shell tests: BehaviorCompiler / HavokFile / Validate.
// Exercises the editor-facing wrapper layer directly (the deep Build→Serialize→
// Deserialize path is covered by builder_test / roundtrip_test / m2_oracle_test).

#include "test_harness.h"

#include "havok/classes/Classes.h"
#include "havok/core/PackFileSerializer.h"
#include "havok/model/BehaviorData.h"
#include "havok/sct/BehaviorCompiler.h"
#include "havok/sct/HavokFile.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <system_error>
#include <vector>

using namespace havok;

namespace {

// A minimal valid behavior packfile, built straight from Tier-A classes.
std::vector<std::uint8_t> SerializeMinimalGraph() {
    auto clip = std::make_shared<hkbClipGenerator>();
    clip->m_name = "C"; clip->m_animationName = "anim.hkx";
    clip->m_playbackSpeed = 1.0f; clip->m_animationBindingIndex = -1; clip->m_mode = 1;

    auto si = std::make_shared<hkbStateMachineStateInfo>();
    si->m_name = "S"; si->m_probability = 1.0f; si->m_enable = true; si->m_generator = clip;

    auto sm = std::make_shared<hkbStateMachine>();
    sm->m_name = "SM"; sm->m_states.push_back(si);

    auto data = std::make_shared<hkbBehaviorGraphData>();
    data->m_stringData            = std::make_shared<hkbBehaviorGraphStringData>();
    data->m_variableInitialValues = std::make_shared<hkbVariableValueSet>();

    auto bg = std::make_shared<hkbBehaviorGraph>();
    bg->m_name = "SctTest.hkb"; bg->m_rootGenerator = sm; bg->m_data = data;

    auto root = std::make_shared<hkRootLevelContainer>();
    hkRootLevelContainerNamedVariant nv;
    nv.m_name = "hkbBehaviorGraph"; nv.m_className = "hkbBehaviorGraph"; nv.m_variant = bg;
    root->m_namedVariants.push_back(nv);

    PackFileSerializer ser;
    BinaryWriterEx bw;
    ser.Serialize(root, bw, HKXHeader::SkyrimSE());
    return bw.Data();
}

}  // namespace

void run_sct_tests() {
    const auto good = SerializeMinimalGraph();

    // ── Validate: happy path ──
    {
        const sct::ValidationReport vr = sct::ValidatePackfile(good);
        CHECK(vr.ok);
        CHECK(vr.graphName == "SctTest.hkb");
    }

    // ── Validate: sad paths ──
    CHECK(!sct::ValidatePackfile({}).ok);                                  // empty
    CHECK(!sct::ValidatePackfile(std::vector<std::uint8_t>(32, 0)).ok);    // too small

    // ── HavokFile round-trip ──
    {
        std::error_code ec;
        const auto tmp = std::filesystem::temp_directory_path() / "havok_core_sct_test.hkx";
        std::string err;
        CHECK(sct::WriteHavokFile(tmp, good, &err));
        std::vector<std::uint8_t> readBack;
        CHECK(sct::ReadHavokFile(tmp, readBack, &err));
        CHECK(readBack == good);
        std::filesystem::remove(tmp, ec);
    }

    // ── Shell over Tier B: an empty model must not produce a usable file. Build
    //    yields a graph with no rootGenerator, so CompileBehaviorToFile's validate
    //    gate (or the builder itself) rejects it — and it never throws. ──
    {
        std::error_code ec;
        havok::model::BehaviorData empty;
        const auto out = std::filesystem::temp_directory_path() / "havok_core_sct_empty.hkx";
        const sct::CompileResult cr = sct::CompileBehaviorToFile(empty, out, /*validate*/ true);
        CHECK(!cr.ok);
        CHECK(!cr.error.empty());
        std::filesystem::remove(out, ec);
    }
}
