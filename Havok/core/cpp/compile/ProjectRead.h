#pragma once
// ProjectRead — schema-native read of a Havok behavior *project* .hkx into an editable
// ProjectSpec. A project is NOT a behavior graph: it's hkRootLevelContainer ->
// hkbProjectData -> hkbProjectStringData, a flat string-table record. This reads it off
// the havok-io generic SchemaObject graph (MakeSchemaFactory over the shared registry) —
// NO typed hkb* classes — the same schema path the runtime compile + AnimationDecompiler use.
//
// Drained out of the quarantined havok-core (was ProjectCompiler.cpp's typed ReadProject).
// Namespace kept havok::sct (normalization deferred to the mechanical pass).

#include <codec/serialization/packfile/PackFileTypes.h>   // HKXHeader
#include "havok/model/ProjectData.h"    // model::ProjectSpec (aliased below)

#include <cstdint>
#include <string>
#include <vector>

namespace havok::sct {

using havok::model::ProjectSpec;

struct ProjectReadResult {
    bool        ok = false;
    std::string error;      // populated when !ok
    ProjectSpec spec;
    HKXHeader   header;     // the packfile's own header (feed back to re-serialize byte-exact)
};

// Deserialize a project .hkx's bytes into an editable ProjectSpec (+ its header). Never throws
// (any structural failure -> ok=false, message in error).
ProjectReadResult ReadProject(const std::vector<std::uint8_t>& bytes);

} // namespace havok::sct
