#pragma once
// Neutral project Def model — the editable, format-agnostic view of a Havok behavior *project*
// (hkbProjectData -> hkbProjectStringData). It lives in havok-model (the neutral model layer, next
// to BehaviorData) so the schema assembler (model::AssembleProject) AND havok-core's typed
// BuildProject/ReadProject consume ONE definition. havok-core is being retired; the Def belongs on
// this side of the line, and the schema emitter can then own the whole project path.

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace havok::model {

// hkbTransitionEffect::EventMode values. Vanilla projects use EVENT_MODE_IGNORE_FROM_GENERATOR (2).
inline constexpr std::int8_t EVENT_MODE_DEFAULT               = 0;
inline constexpr std::int8_t EVENT_MODE_IGNORE_FROM_GENERATOR = 2;

// Editable view of a project's serialized state. rootPath is SERIALIZE_IGNORED in the file
// (always null), so it is not represented here.
struct ProjectSpec {
    std::array<float, 4>     worldUpWS{ 0.f, 0.f, 1.f, 0.f };
    std::int8_t              defaultEventMode = EVENT_MODE_IGNORE_FROM_GENERATOR;
    std::vector<std::string> animationFilenames;   // usually empty
    std::vector<std::string> behaviorFilenames;    // usually empty
    std::vector<std::string> characterFilenames;   // the field BR sets
    std::vector<std::string> eventNames;           // usually empty
    std::string              animationPath;         // usually empty
    std::string              behaviorPath;          // usually empty
    std::string              characterPath;         // usually empty
    std::string              fullPathToSource;      // usually empty
};

}  // namespace havok::model
