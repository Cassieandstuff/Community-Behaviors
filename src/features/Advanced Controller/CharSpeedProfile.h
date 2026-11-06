#pragma once

#include "CharMovementModel.h"

/// @file
/// @brief The speeds CB's controller moves at: settings.ini's `[Speed.<MNAM>]` over built-in defaults
///        (locomotion/SpeedProfile.h), in place of the movement type records' speeds, for the actors CB drives only.
/// @note The engine's speed is rescaled rather than replaced, by the profile's speed over the record's: every multiplier
///       the engine laid on the record (the actor's scale, SpeedMult, armor weight on running) stays. Actors CB doesn't
///       drive keep the records' speeds, which their vanilla clips were made for.

namespace CB::feature::controller::speed {

    /// @brief `engine`, the engine's forward speed for `gait` in `actor`'s current movement type, taken from the record's
    ///        speed to the profile's. `engine` unchanged when the type has no profile.
    float Forward(RE::Actor* actor, Gait gait, float engine);

    /// @brief The profile's side and back speeds as fractions of its forward one, over what `caps` holds; a direction the
    ///        profile leaves unset keeps its value.
    void Ratios(RE::Actor* actor, Gait gait, move::Caps& caps);

}  // namespace CB::feature::controller::speed
