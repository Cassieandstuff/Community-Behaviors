#pragma once

#include <string>
#include <vector>

namespace CB::engine::compile::trace {

// ── Runtime watch list (the RUNTIME half of a probe) ─────────────────────────────
// A probe file may also carry a `watch:` seq — behavior-graph variables to sample in-game per frame
// (see the debug/ README). This is pure DATA extraction (no engine deref, no sink): the plugin's
// runtime trace reads the returned names off the live player graph. `kind` is "int" | "float" | "bool"
// (default "int"). Scans every enabled *.yaml in `debugDir`, dedups by var name. Never throws.
struct WatchVar {
    std::string var;            // graph-variable name (case-preserved — Havok is case-sensitive)
    std::string kind = "int";   // int | float | bool
};
std::vector<WatchVar> LoadRuntimeWatch(const std::string& debugDir, std::string* warn = nullptr);

} // namespace CB::engine::compile::trace
