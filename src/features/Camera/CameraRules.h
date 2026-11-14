#pragma once

#include <hky/locomotion/Modifiers.h>

#include <cstddef>
#include <cstdint>
#include <limits>

/// @file
/// @brief The camera rules (camera_rules): SKSE/Plugins/Community Behaviors/Camera/*.json, the same rule engine as the
///        movement rules (HKY-SDK locomotion/Modifiers.h) over the camera's own outputs, evaluated against the player
///        each frame CB's camera drives the view (Camera.cpp).
/// @note  Two halves: the zoom outputs are goals for CB's zoom (CameraZoom.h), which eases on its own; the rest (FOV,
///        shoulder, anchor lag) are eased by the rules and laid on the rig's numbers.
/// @note  Material groups are the movement rules' (relay::SetMaterialGroups is theirs): a camera file declaring its own
///        is refused.

namespace CB::feature::camera::rules {

    using hky::locomotion::ModifierState;

    /// @brief The camera rules' outputs, indexes into kSchema.
    enum class Output : std::uint8_t {
        Distance,            ///< scale: the zoom's whole range and preferred distance
        MaxDistance,         ///< cap: the farthest the zoom may go, game units
        ZoomMinDistance,     ///< set: the nearest the zoom may go (fVanityModeMinDist)
        ZoomMaxDistance,     ///< set: the farthest the zoom may go (fVanityModeMaxDist)
        PreferredDistance,   ///< set: where the zoom goes when the rule starts (the player's preferred zoom otherwise)
        Fov,                 ///< set: the world field of view, degrees
        FovScale,            ///< scale: the world field of view
        ShoulderScale,       ///< scale: the shoulder offset's sideways and vertical parts
        ShoulderX,           ///< add: game units to the right
        ShoulderZ,           ///< add: game units up
        AnchorLag,           ///< scale: the anchor's follow halflives
        Count
    };
    inline constexpr std::size_t Index(Output o) { return static_cast<std::size_t>(o); }

    /// @brief Every output eases over 0.4 s unless its rule says otherwise.
    extern const hky::locomotion::RuleSchema kSchema;

    /// @brief This frame's zoom goals from the rules holding, unsmoothed (the zoom eases itself); NaN = no rule states it.
    struct ZoomGoals {
        float scale     = 1.0f;
        float min       = std::numeric_limits<float>::quiet_NaN();
        float max       = std::numeric_limits<float>::quiet_NaN();
        float cap       = std::numeric_limits<float>::quiet_NaN();
        float preferred = std::numeric_limits<float>::quiet_NaN();
        /// @brief The rules stating a zoom output differ from last frame's: the zoom heads for the new preferred distance.
        bool  changed = false;
    };

    struct Result {
        ModifierState state{ kSchema };   ///< the eased outputs (FOV, shoulder, anchor lag)
        ZoomGoals     zoom;
    };

    /// @brief Evaluates the camera rules for `player` and eases them by `dt`; neutral while the rules are off or none is
    ///        loaded.
    Result Step(RE::Actor& player, float dt);

    /// @brief Re-reads the rule files. Thread-safe; the next Step uses the new set.
    void Reload();

}  // namespace CB::feature::camera::rules
