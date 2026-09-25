#pragma once
// ConfigModel.h — the authored conditional-setdata config (Inc 2).
//
// A config is the addressable unit (keyed by relative-path id); it carries a condition EXPRESSION, a
// from->to replacement map (relative paths from meshes), an enabled default, and an optional target
// archetype. OAR-in / hybrid-canonical (spec §4). Parse (from the hky/loose) → ComposeConfigs →
// ConditionInstances (this file) + setdata sets (Inc 2b, AnimationSetDataServer).
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace CB::conditions {

    struct ReplaceRule { std::string from; std::string to; };   // relative paths from `meshes`

    struct ConditionConfig {
        std::string id;                    // stable id (relative-path-ish); seeds gate name + Random
        std::string when;                  // condition expression (the DSL)
        std::string target;                // optional project/archetype; empty = broad
        bool        enabled = true;        // default toggle state
        std::vector<ReplaceRule> replace;  // from->to replacement map
    };

    // Deterministic identity from a config id — compose AND set-emission (Inc 2b) must agree on both.
    std::uint32_t ConfigId(std::string_view id);            // fnv32(id)
    std::string   GateNameForConfig(std::string_view id);   // "CBCond_" + hex8(ConfigId)

    // Parse one config document (YAML). Returns false + fills `err` on malformed input.
    bool ParseConditionConfig(std::string_view yaml, ConditionConfig& out, std::string& err);

    // Compose a LOAD-ORDERED config list: harvest+liveness by id (later overrides earlier), compile each
    // `when`, register a ConditionInstance per live config (clears prior instances first). Records to
    // `log`. The per-config toggle store is owned internally; instance.enabled points into it.
    struct ComposeStats { int total = 0; int registered = 0; int failed = 0; };
    ComposeStats ComposeConfigs(std::span<const ConditionConfig> ordered, std::string& log);

}  // namespace CB::conditions
