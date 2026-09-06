#pragma once
#include "havok/core/PackFileTypes.h"
#include "havok/sct/BehaviorCompiler.h"  // CompileResult
#include "havok/model/ProjectData.h"     // model::ProjectSpec (moved to havok-model — see below)

#include <array>
#include <cstdint>
#include <string>
#include <vector>

// ProjectCompiler (SCT shell) — first-party read / build / rewrite of a Havok
// behavior *project* .hkx (hkRootLevelContainer -> hkbProjectData ->
// hkbProjectStringData). A project is NOT a behavior graph: these calls go
// straight through PackFileDeserializer / PackFileSerializer, never
// BehaviorBuilder.
//
// BR uses BuildProject (synthesize from constants — it has no BSA reader to read
// the vanilla bytes). The editor / offline gate use ReadProject + RoundTripProject
// (byte-exact correctness proof) and RewriteProjectCharacterFilenames.
//
// defaultEventMode values are hkbTransitionEffect::EventMode. Vanilla projects use
// EVENT_MODE_IGNORE_FROM_GENERATOR == 2.

namespace havok::sct {

// ProjectSpec + the EVENT_MODE_* constants now live in havok-model (havok/model/ProjectData.h) — the
// neutral Def layer, next to BehaviorData — so the schema assembler and this typed shell share ONE
// definition as havok-core retires. Aliased here so existing havok::sct::ProjectSpec / EVENT_MODE_*
// uses keep compiling unchanged.
using havok::model::ProjectSpec;
using havok::model::EVENT_MODE_DEFAULT;
using havok::model::EVENT_MODE_IGNORE_FROM_GENERATOR;

struct ProjectReadResult {
    bool        ok = false;
    std::string error;      // populated when !ok
    ProjectSpec spec;
    HKXHeader   header;     // the packfile's own header (feed back to re-serialize byte-exact)
};

// Deserialize a project .hkx's bytes into an editable ProjectSpec (+ its header).
// Never throws.
ProjectReadResult ReadProject(const std::vector<std::uint8_t>& bytes);

// Build a complete valid project .hkx from a spec. This is BR's synthesize path —
// no source bytes needed. The default header IS the SkyrimSE project preset
// (hk_2010.2.0-r1, 64-bit), byte-compatible with every vanilla *project.hkx.
CompileResult BuildProject(const ProjectSpec& spec,
                           const HKXHeader& header = HKXHeader::SkyrimSE());

// Deserialize `bytes` and re-serialize with the SAME header — the byte-exact
// round-trip used as the layout correctness proof. Result bytes should equal the
// input for any well-formed vanilla project.
CompileResult RoundTripProject(const std::vector<std::uint8_t>& bytes);

// Deserialize vanilla project bytes, replace the characterFilenames table wholesale
// (the whole vector), and re-serialize — everything else byte-identical except the
// changed string(s) and the offsets they shift. `newCharacterFilenames` must have
// the same COUNT as the original (a project's characterFilenames count is fixed by
// its actor); pass the rewritten strings in the same order.
CompileResult RewriteProjectCharacterFilenames(const std::vector<std::uint8_t>& bytes,
                                               const std::vector<std::string>& newCharacterFilenames);

// Convenience: rewrite only characterFilenames[0] (the common single-entry case).
CompileResult RewriteProjectCharacterFilename(const std::vector<std::uint8_t>& bytes,
                                              const std::string& newFirst);

// project.yaml serde — the two directions co-located (omnidirectional SOP), plain
// text (no YAML lib) so they round-trip 1:1. The CLI decompile path is
// ReadProject(bytes) -> EmitProjectYaml; the compile path is ParseProjectYaml ->
// BuildProject. ParseProjectYaml returns false only on malformed input.
std::string EmitProjectYaml(const ProjectSpec& spec);
bool        ParseProjectYaml(const std::string& text, ProjectSpec& out);

} // namespace havok::sct
