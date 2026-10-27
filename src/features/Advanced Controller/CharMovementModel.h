#pragma once

#include "CharFacing.h"

#include <locomotion/Movement.h>

/// @file
/// @brief The player's movement model: the engine's pure step (locomotion/Movement.h), shared by the frame (CharFacing:
///        real dt, live input) and the predictor (its substeps, input frozen), so the movement predicted is the
///        movement played; this half holds what is the game's (the per-actor snapshot table, the flag).

namespace CB::feature::controller::move {

    using CB::engine::locomotion::Mode;
    using CB::engine::locomotion::Profile;
    using CB::engine::locomotion::kPlayer;
    using CB::engine::locomotion::Caps;
    using CB::engine::locomotion::Input;
    using CB::engine::locomotion::State;
    using CB::engine::locomotion::MaxSpeed;
    using CB::engine::locomotion::Step;
    using CB::engine::locomotion::SprintDirectionAllowed;

    /// @brief The latest frame's model: the state after its step, and what it stepped with.
    struct Snapshot {
        State   state;
        Input   input;
        Caps    caps;
        Profile profile;
        float   px = 0.0f, py = 0.0f;   ///< the actor's position that frame
    };
    /// @brief The player's reference FormID (fixed in every game).
    constexpr RE::FormID kPlayerId = 0x14;

    /// @brief `actor`'s latest snapshot; false while no frame driver holds it (the model off, bMovementModel, or CB's
    ///        camera not driving third person) or no frame of it ran within the last half second.
    /// @note  Thread-safe: each actor's frame writes its own; the predictor (physics), the sprint rules and the HUD read it.
    bool Current(RE::FormID actor, Snapshot& out);
    /// @brief The player's Current.
    bool Current(Snapshot& out);
    /// @brief The frame's half of Current, for `actor`.
    void Publish(RE::FormID actor, const Snapshot& snap);

    /// @brief Whether the movement model runs (char_movement_model).
    bool On();

}  // namespace CB::feature::controller::move
