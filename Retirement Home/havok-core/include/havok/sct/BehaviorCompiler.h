#pragma once
#include "havok/core/PackFileTypes.h"
#include "havok/model/BehaviorData.h"
#include "havok/sct/Validate.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

// BehaviorCompiler (SCT shell) — the editor/blueprint export entry point. Ties
// Tier B (BehaviorBuilder: *Def model → Tier-A object graph) to Tier A
// (PackFileSerializer: object graph → Havok packfile bytes). One call: a loaded
// behavior model in, a valid .hkx out. No .NET, no XML, no subprocess.
//
// Tier discipline: sct → model → classes → core.

namespace havok::sct {

struct CompileResult {
    bool                      ok = false;
    std::string               error;  // populated when !ok
    std::vector<std::uint8_t> bytes;  // the packfile (when ok)
};

// Compile a loaded behavior model straight to packfile bytes. Never throws —
// builder/serializer exceptions are captured into CompileResult::error.
CompileResult CompileBehavior(const model::BehaviorData& data,
                              const HKXHeader& header = HKXHeader::SkyrimSE());

// Data-driven compiler toggle (havok-core-v2 migration): when enabled AND the Havok/ schema tree at
// `schemaDir` loads, CompileBehavior assembles the graph via the schema-driven havok::model::AssembleGraph
// instead of the typed BehaviorBuilder — proven byte-identical offline. Falls back to the typed path on
// any failure (registry missing / assemble throws), so enabling it can never regress a served graph.
// Call once at startup (e.g. from a settings.ini toggle). schemaDir empty → read env SCT_HAVOK_SCHEMA_DIR.
void SetSchemaCompiler(bool enabled, const std::string& schemaDir);
// Whether the schema compiler is enabled AND its registry loaded (for a startup log line).
bool SchemaCompilerReady();
// A human reason the schema compiler is NOT serving — schema tree failed to load, or its
// `schema_version` is incompatible with the version this build speaks (the editor<->compiler
// contract gate). Empty when it loaded cleanly or was never enabled. For a startup log line.
const std::string& SchemaCompilerError();
// Counts of graphs served by each path since process start (for a post-warmup log line).
void SchemaCompilerStats(std::size_t& schemaServed, std::size_t& typedServed);

// Compile, optionally validate (round-trip the bytes), then write to disk.
// Any failing step leaves ok=false with a descriptive error.
CompileResult CompileBehaviorToFile(const model::BehaviorData&   data,
                                    const std::filesystem::path& outPath,
                                    bool                         validate = true,
                                    const HKXHeader&             header = HKXHeader::SkyrimSE());

} // namespace havok::sct
