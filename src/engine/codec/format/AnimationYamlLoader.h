#pragma once
/// @file
/// @brief An animation unit's text <-> AnimationDef, through the schema: the `animation:` map is a CBAnimation
///        (schema/animation/CBAnimation.yaml) read by ParseEmbeddedObject and written by EmitEmbeddedObject.
/// @note  Units written before CBAnimation read unchanged (the schema mirrors their keys); only their `motion: none`
///        needs the loader's shim (CBAnimation.rootMotionVisual).

#include "interface/AnimationDef.h"

#include <filesystem>
#include <string>

namespace CB::engine::anim {

struct AnimationYamlLoader {
    // Throws std::runtime_error on read/parse failure.
    static AnimationDef Load(const std::filesystem::path& yamlFile);

    // Parse from an in-memory YAML string (a packed .hky vends its unit YAML as text, never
    // a disk path). `sourceName` is used only in error messages. Throws on parse failure.
    static AnimationDef LoadFromString(const std::string& yamlText, const std::string& sourceName = "<memory>");
};

/// @brief `def` as a unit's text: `animation:` with the CBAnimation's fields as its children (2-space indent).
/// @throws std::runtime_error without the shared schema registry.
std::string EmitAnimationYaml(const AnimationDef& def);

/// @brief `curves` as a `  curves:` block (indented as a child of `animation:`), written through the schema
///        (CBAnimationCurves) for a caller appending curves to a unit's text. Empty for no curves.
/// @throws std::runtime_error without the shared schema registry.
std::string EmitAnimCurvesYaml(const std::vector<AnimCurveDef>& curves);

} // namespace CB::engine::anim
