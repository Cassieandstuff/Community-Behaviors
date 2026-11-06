#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace CB::engine::locomotion {

    /// @brief One gait's speeds in game units/s; 0 = unset, the movement type's own speed stands.
    /// @note Left and right are one value: vanilla's records differ between them by fractions of a unit, which no clip
    ///       set mirrors.
    struct DirectionSpeeds {
        float forward = 0.0f, side = 0.0f, back = 0.0f;
    };

    /// @brief The speeds CB's controller moves at in one movement type (a MOVT, named by its MNAM, the graph's
    ///        `iState_<name>` suffix), in place of the record's.
    /// @note The run speeds serve running and sprinting: sprinting is its own movement type, whose run column is the sprint.
    struct SpeedProfile {
        DirectionSpeeds walk, run;
    };

    /// @brief The settings.ini section holding a movement type's speeds: `[Speed.<MNAM>]`, keys fWalkForward, fWalkSide,
    ///        fWalkBack, fRunForward, fRunSide, fRunBack.
    std::string SpeedSection(std::string_view type);

    /// @brief The built-in speeds for `type`: GASP's neutral loops as measured (walk 148/133/111, run 370/259/222, sprint
    ///        518; sneaking on its crouch loops), for the locomotion types the pose search replaces. None for the rest.
    std::optional<SpeedProfile> DefaultSpeedProfile(std::string_view type);

    /// @brief `type`'s speeds: the built-in ones with each key `lookup` answers (a non-empty number) laid over them.
    /// @param lookup the value of a key in the type's section, empty when absent
    /// @return none when neither the built-ins nor the section state a speed: the record's speeds stand untouched.
    std::optional<SpeedProfile> ResolveSpeedProfile(std::string_view type, const std::function<std::string(std::string_view)>& lookup);

    /// @brief The factor taking the movement type's own speed to the profile's: `wanted / record`, 1 when either is unset.
    float SpeedFactor(float wanted, float record);

}  // namespace CB::engine::locomotion
