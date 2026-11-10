#pragma once

#include "CharMovementModel.h"

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
        float CB::engine::locomotion::Profile::*member;
        float                       shown = 1.0f;   ///< ini and menu value = profile value x this
        float                       max   = 0.0f;   ///< the menu's slider ceiling, in shown units
    };

    /// @brief Every tunable number, in the menu's order.
    std::span<const Field> Fields();

    /// @brief The profile now: GASP's defaults under settings.ini `[Movement]`, then whatever Set gave since.
    /// @note  Read per actor per frame from job threads: a copy, under a lock.
    CB::engine::locomotion::Profile Current();

    /// @brief Use `profile` from the next frame; Save writes it.
    void Set(const CB::engine::locomotion::Profile& profile);

    /// @brief Write every field that differs from GASP's default to settings.ini `[Movement]`, removing the rest.
    /// @return false when the file could not be written (logged).
    bool Save();

    /// @brief Back to GASP's defaults, live; Save then clears the section.
    void Reset();

}  // namespace CB::feature::controller::tuning
