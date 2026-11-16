#pragma once

#include "CharMovementModel.h"

#include <hky/locomotion/BodyFacing.h>

#include <span>
#include <string_view>

/// @file
/// @brief The movement model's numbers (acceleration, friction, braking, turning, grip) for the player and the NPCs CB
///        drives: settings.ini `[Movement]` over GASP's defaults (locomotion::Profile), changed live from the config menu.

namespace CB::feature::controller::tuning {

    /// @brief One tunable number: its settings.ini key, its label, its member of the profile, and the factor the menu and
    ///        the ini show it in (degrees for the angular rates, 1 otherwise).
    struct Field {
        std::string_view            key, label, unit;
        float hky::locomotion::Profile::*member;
        float                       shown = 1.0f;   ///< ini and menu value = profile value x this
        float                       max   = 0.0f;   ///< the menu's slider ceiling, in shown units
    };

    /// @brief Every tunable number, in the menu's order.
    std::span<const Field> Fields();

    /// @brief The profile now: GASP's defaults under settings.ini `[Movement]`, then whatever Set gave since.
    /// @note  Read per actor per frame from job threads: a copy, under a lock.
    hky::locomotion::Profile Current();

    /// @brief Use `profile` from the next frame; Save writes it.
    void Set(const hky::locomotion::Profile& profile);

    /// @brief Write every field that differs from GASP's default to settings.ini `[Movement]`, removing the rest.
    /// @return false when the file could not be written (logged).
    bool Save();

    /// @brief Back to GASP's defaults, live; Save then clears the section.
    void Reset();

    /// @brief One tunable number of the player's body facing (locomotion::BodyFacingProfile), as Field.
    struct BodyField {
        std::string_view                              key, label, unit;
        float hky::locomotion::BodyFacingProfile::*member;
        float                                         shown = 1.0f;
        float                                         max   = 0.0f;
    };

    /// @brief Every body facing number, in the menu's order.
    std::span<const BodyField> BodyFields();

    /// @brief The player's body facing now: locomotion::kPlayerBody under settings.ini `[Character]`, then whatever SetBody
    ///        gave since. Thread-safe, a copy.
    hky::locomotion::BodyFacingProfile Body();

    /// @brief Use `profile` from the next frame; SaveBody writes it.
    void SetBody(const hky::locomotion::BodyFacingProfile& profile);

    /// @brief Write every body field that differs from the default to settings.ini `[Character]`, removing the rest.
    bool SaveBody();

    /// @brief Back to locomotion::kPlayerBody, live.
    void ResetBody();

    /// @brief settings.ini `[Character] bWeightedFacing` (flag char_weighted_facing), live: the player's heading is the body
    ///        facing (the clip's root yaw, steered toward the model's facing and clamped to it) instead of the model's.
    bool WeightedFacing();

    /// @brief settings.ini `[Character] bPlayerTraction` (flag char_player_traction), live: the player's profile takes
    ///        locomotion::kPlayerTraction for every grip and dig-in number `[Movement]` leaves at 0.
    bool PlayerTraction();

    /// @brief Switch WeightedFacing / PlayerTraction from the next frame and write the key to settings.ini `[Character]`.
    void SetWeightedFacing(bool on);
    void SetPlayerTraction(bool on);

}  // namespace CB::feature::controller::tuning
