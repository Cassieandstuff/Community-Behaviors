#pragma once
// AnimationYamlLoader — parse an animation.yaml into an AnimationDef.
// Mirrors HKBuild's Models/AnimationDef.cs YAML schema:
//
//   animation:            # (the "animation:" wrapper is optional)
//     name: ...
//     duration: 1.5
//     skeleton: ...
//     compression: { rotationTolerance, translationTolerance, scaleTolerance,
//                    rotationDegree, translationDegree, scaleDegree, maxFramesPerBlock }
//     tracks:
//       - bone: NPC Root
//         translation: [ { time: 0.0, value: [x,y,z] }, ... ]
//         rotation:    [ { time: 0.0, value: [x,y,z,w] }, ... ]
//         scale:       [ { time: 0.0, value: [x,y,z] }, ... ]
//     floatTracks:
//       - name: ...
//         keyframes: [ { time: 0.0, value: 0.5 }, ... ]
//
// ryml-gated (VS/vcpkg build only).

#include "havok/anim/AnimationDef.h"

#include <filesystem>
#include <string>

namespace havok::anim {

struct AnimationYamlLoader {
    // Throws std::runtime_error on read/parse failure.
    static AnimationDef Load(const std::filesystem::path& yamlFile);

    // Parse from an in-memory YAML string (a packed .hky vends its unit YAML as text, never
    // a disk path). `sourceName` is used only in error messages. Throws on parse failure.
    static AnimationDef LoadFromString(const std::string& yamlText, const std::string& sourceName = "<memory>");
};

} // namespace havok::anim
