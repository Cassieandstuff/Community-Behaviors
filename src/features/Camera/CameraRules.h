#pragma once

#include <hky/locomotion/Modifiers.h>

#include <cstddef>
#include <cstdint>

/// @file
/// @brief The camera rules (camera_rules): SKSE/Plugins/Community Behaviors/Camera/*.json, the same rule engine as the
///        movement rules (HKY-SDK locomotion/Modifiers.h) over the camera's own outputs, evaluated against the player
///        each frame CB's camera drives the view and laid onto the rig's numbers (Camera.cpp).
/// @note  Material groups are the movement rules' (relay::SetMaterialGroups is theirs): a camera file declaring its own
///        is refused.

namespace CB::feature::camera::rules {

    using hky::locomotion::ModifierState;

    /// @brief The camera rules' outputs, indexes into kSchema.
    enum class Output : std::uint8_t {
        Distance,          ///< scale: the boom's length
        MaxDistance,       ///< cap: the boom's length, game units
        ZoomMinDistance,   ///< set: the boom's length at the wheel's nearest (fVanityModeMinDist)
        ZoomMaxDistance,   ///< set: the boom's length at the wheel's farthest (fVanityModeMaxDist)
        ForcedDistance,    ///< set: the boom's length whatever the wheel says
        Fov,               ///< set: the world field of view, degrees
        FovScale,          ///< scale: the world field of view
        ShoulderScale,     ///< scale: the shoulder offset's sideways and vertical parts
        ShoulderX,         ///< add: game units to the right
        ShoulderZ,         ///< add: game units up
        AnchorLag,         ///< scale: the anchor's follow halflives
        Count
    };
    inline constexpr std::size_t Index(Output o) { return static_cast<std::size_t>(o); }

    /// @brief Every output eases over 0.4 s unless its rule says otherwise.
    extern const hky::locomotion::RuleSchema kSchema;

    /// @brief Evaluates the camera rules for `player` and eases them by `dt`.
    /// @return the outputs to lay on this frame; neutral while the rules are off or none is loaded.
    ModifierState Step(RE::Actor& player, float dt);

    /// @brief Re-reads the rule files. Thread-safe; the next Step uses the new set.
    void Reload();

}  // namespace CB::feature::camera::rules
