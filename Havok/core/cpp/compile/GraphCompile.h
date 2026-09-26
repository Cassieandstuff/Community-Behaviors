#pragma once
// GraphCompile — the schema-only compile-to-bytes primitives: a merged model in, packfile bytes
// out, assembled through the Havok/ schema descriptors (havok::model::AssembleGraph / AssembleCharacter
// / AssembleProject) and serialized. NO typed hkb* builders, NO toggle, NO typed fallback — this is the
// runtime's compile path after the firesale.
//
// On any schema failure (registry not armed, assemble returns null, or a throw) the call returns
// ok=false with a descriptive error; the caller (the Resolver) writes no cache bytes, so the ByteServe
// hook passes the open through to the VANILLA file (safe degradation) and logs the vanilla'd graph.
// This REPLACES the old havok::sct::CompileBehavior/CompileCharacter/BuildProject facades, whose typed
// BehaviorBuilder/CharacterBuilder fallback stays in havok-core purely as the offline schema-vs-typed
// gate oracle (dead at firesale step 5) — the runtime no longer touches it.
//
// The heavy lifting (field traversal) lives in havok::model::Assemble* (havok-model), shared with the
// gate harness's schema branch, so the two can't drift.

#include <codec/serialization/packfile/PackFileTypes.h>   // HKXHeader

#include <cstdint>
#include <string>
#include <vector>

namespace havok::model { struct BehaviorData; struct CharacterData; struct ProjectSpec; }

namespace CB::core::compile {

struct CompileResult {
    bool                      ok = false;
    std::string               error;  // populated when !ok (the reason to serve vanilla)
    std::vector<std::uint8_t> bytes;  // the packfile (when ok)
};

// Compile a merged behavior graph straight to packfile bytes (ResolveBehaviorBindings -> AssembleGraph
// -> serialize). Never throws.
CompileResult CompileBehavior(const havok::model::BehaviorData& data,
                              const havok::HKXHeader& header = havok::HKXHeader::SkyrimSE());

// Compile a merged character straight to packfile bytes (AssembleCharacter -> serialize). Never throws.
CompileResult CompileCharacter(const havok::model::CharacterData& data,
                               const havok::HKXHeader& header = havok::HKXHeader::SkyrimSE());

// Synthesize a project packfile from a spec (AssembleProject -> serialize). Never throws.
CompileResult BuildProject(const havok::model::ProjectSpec& spec,
                           const havok::HKXHeader& header = havok::HKXHeader::SkyrimSE());

// Count of graphs+characters successfully schema-compiled since process start (warm-up telemetry;
// the typed path is retired, so this is the whole story). Thread-safe.
std::size_t CompiledCount();

} // namespace CB::core::compile
