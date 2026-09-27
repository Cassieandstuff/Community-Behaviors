#pragma once
// GraphCompile — the schema-only compile-to-bytes primitives: a merged model in, packfile bytes
// out, assembled through the Havok/ schema descriptors (CB::core::compile::AssembleGraph / AssembleCharacter
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
// The heavy lifting (field traversal) lives in CB::core::compile::Assemble* (havok-model), shared with the
// gate harness's schema branch, so the two can't drift.

#include <codec/serialization/packfile/PackFileTypes.h>   // HKXHeader

#include <cstdint>
#include <string>
#include <vector>

namespace CB::core::common { struct BehaviorData; struct CharacterData; struct ProjectSpec; }

namespace CB::core::compile {

struct CompileResult {
    bool                      ok = false;
    std::string               error;  // populated when !ok (the reason to serve vanilla)
    std::vector<std::uint8_t> bytes;  // the packfile (when ok)
};

// Compile a merged behavior graph straight to packfile bytes (ResolveBehaviorBindings -> AssembleGraph
// -> serialize). Never throws.
CompileResult CompileBehavior(const CB::core::common::BehaviorData& data,
                              const CB::core::codec::HKXHeader& header = CB::core::codec::HKXHeader::SkyrimSE());

// Compile a merged character straight to packfile bytes (AssembleCharacter -> serialize). Never throws.
CompileResult CompileCharacter(const CB::core::common::CharacterData& data,
                               const CB::core::codec::HKXHeader& header = CB::core::codec::HKXHeader::SkyrimSE());

// Synthesize a project packfile from a spec (AssembleProject -> serialize). Never throws.
CompileResult BuildProject(const CB::core::common::ProjectSpec& spec,
                           const CB::core::codec::HKXHeader& header = CB::core::codec::HKXHeader::SkyrimSE());

// Count of graphs+characters successfully schema-compiled since process start (warm-up telemetry;
// the typed path is retired, so this is the whole story). Thread-safe.
std::size_t CompiledCount();

} // namespace CB::core::compile
