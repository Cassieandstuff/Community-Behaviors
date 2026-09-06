#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Validate (SCT shell) — structural soundness check for serialized packfile bytes.
// Round-trips the bytes through havok-core's own deserializer and confirms the
// result is a usable behavior packfile. Cheap gate the editor/exporter runs after
// a compile, before writing to disk or handing the file to Engine Relay.

namespace havok::sct {

struct ValidationReport {
    bool        ok = false;
    std::string error;      // populated when !ok
    std::string graphName;  // the hkbBehaviorGraph.name, when resolvable (informational)
};

// Deserialize `bytes` and verify: root is hkRootLevelContainer with ≥1 variant,
// the first variant is an hkbBehaviorGraph, and it has a rootGenerator + data.
ValidationReport ValidatePackfile(const std::vector<std::uint8_t>& bytes);

} // namespace havok::sct
