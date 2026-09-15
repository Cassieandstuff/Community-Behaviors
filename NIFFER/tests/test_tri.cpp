#include "test_harness.h"

#include <niffer/Niffer.h>

#include <cstring>
#include <vector>

using namespace niffer;

// Hand-build a minimal FRTRI003 (header + tiny geometry + one diff morph),
// Save->Load->Save, assert byte-identity + field fidelity.
void run_tri_tests() {
    std::printf(" tri\n");

    TriFile t;
    t.vertexNum = 2;
    t.faceNum = 0;
    t.uvVertexNum = 0;   // no UV -> no uv sections
    t.morphNum = 1;
    t.addMorphNum = 0;
    t.addVertexNum = 0;
    t.reservedA.assign(12, 0);
    t.reservedB.assign(20, 0);
    t.baseVertices.assign(static_cast<size_t>(t.vertexNum) * 12, 0);  // 2 verts
    // faceIndices empty (faceNum 0); no uv
    TriMorph m;
    m.name = std::string("Aah\0", 4);   // len-prefixed raw incl null
    m.baseDiff = 0.5f;
    m.deltas.assign(static_cast<size_t>(t.vertexNum) * 6, 7);  // int16[3] per vert
    t.diffMorphs.push_back(m);

    std::vector<uint8_t> bytes = t.Save();
    CHECK(std::memcmp(bytes.data(), "FRTRI003", 8) == 0);

    auto loaded = TriFile::Load(bytes);
    CHECK(loaded.has_value());
    if (loaded) {
        CHECK_EQ(loaded->vertexNum, 2);
        CHECK_EQ(loaded->morphNum, 1);
        CHECK_EQ(loaded->diffMorphs.size(), 1u);
        if (!loaded->diffMorphs.empty()) {
            CHECK(loaded->diffMorphs[0].name == std::string("Aah\0", 4));
            CHECK(loaded->diffMorphs[0].baseDiff == 0.5f);
            CHECK_EQ(loaded->diffMorphs[0].deltas.size(), 12u);
        }
        CHECK(loaded->Save() == bytes);   // byte-identical re-write
    }

    // A non-FRTRI003 buffer is rejected.
    std::vector<uint8_t> junk(64, 0);
    auto bad = TriFile::Load(junk);
    CHECK(!bad.has_value());
}
