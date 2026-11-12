#pragma once

#include "CharMovementModel.h"

#include <hky/locomotion/SpeedProfile.h>

#include <optional>
#include <string>
#include <vector>

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

    /// @brief `run`, a forward run speed in `actor`'s current movement type (Forward's, every multiplier kept), scaled to the
    ///        type's walk: the profile's walk over its run, else the record's.
    float WalkOfRun(RE::Actor* actor, float run);

    /// @brief The gait a forward speed (Forward's) reads as: Walk up to halfway between the type's walk and run, else Run.
    /// @note  For an AI moving with neither the walk nor the run flag raised: CB's chooser needs a gait to pick databases.
    Gait GaitOfSpeed(RE::Actor* actor, float speed);

    /// @brief What the config page shows of one movement type.
    struct TypeView {
        std::string                                    name;      ///< the MNAM, byte-exact
        std::optional<hky::locomotion::SpeedProfile> profile;   ///< live: what the controller uses now
        bool                                           builtIn = false;   ///< CB has defaults for it
        bool                                           record  = false;   ///< a movement type record of that name exists
        hky::locomotion::SpeedProfile           stated;    ///< the record's own speeds (forward/left/back), when `record`
    };

    /// @brief Every type with built-ins, then every other type an actor CB drives has been in. Thread-safe.
    std::vector<TypeView> Types();

    /// @brief `actor`'s current movement type's name; empty without one.
    std::string CurrentType(RE::Actor* actor);

    /// @brief Use `profile` for `type` from the next frame on, without saving it.
    void Set(const std::string& type, const hky::locomotion::SpeedProfile& profile);

    /// @brief Write `type`'s live speeds to settings.ini's `[Speed.<type>]`; false when the file could not be written.
    bool Save(const std::string& type);

    /// @brief Remove `type`'s keys from settings.ini and go back to the built-ins (none: the record's speeds).
    bool Reset(const std::string& type);

}  // namespace CB::feature::controller::speed
