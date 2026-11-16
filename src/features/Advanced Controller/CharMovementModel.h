#pragma once

#include "CharFacing.h"

#include <hky/locomotion/Movement.h>

#include <array>

/// @file
/// @brief The player's movement model: the engine's pure step (locomotion/Movement.h), shared by the frame (CharFacing:
///        real dt, live input) and the predictor (its substeps, input frozen), so the movement predicted is the
///        movement played; this half holds what is the game's (the per-actor snapshot table, the flag).

namespace CB::feature::controller::move {

    using hky::locomotion::Mode;
    using hky::locomotion::Profile;
    using hky::locomotion::kPlayer;
    using hky::locomotion::Caps;
    using hky::locomotion::Input;
    using hky::locomotion::State;
    using hky::locomotion::MaxSpeed;
    using hky::locomotion::Step;
    using hky::locomotion::SprintDirectionAllowed;

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

    /// @brief The model's facing ahead, as the predictor steps it on from a snapshot with the input frozen: what the body
    ///        facing steers toward (GASP's Steering target, the trajectory's facing at +0.5 s).
    struct FacingAhead {
        static constexpr int kPoints = 6;   ///< the predictor's path points over its 1 s horizon
        float step = 0.0f;                  ///< seconds between points
        /// @brief State::yaw (unwrapped near the actor's): [0] the snapshot's, [i] i * step seconds on.
        std::array<float, kPoints + 1> yaw{};
    };

    /// @brief The predictor's half of FacingAt, for `actor`.
    void PublishFacingAhead(RE::FormID actor, const FacingAhead& ahead);

    /// @brief `actor`'s model facing `seconds` ahead (at most the predictor's horizon), interpolated between its points.
    /// @return false when no prediction of it was published within the last half second (the predictor off, or CB not
    ///         driving it): the caller then has no target but the model's facing now
    /// @note  Thread-safe: the predictor (physics) writes it, the frame reads it. Up to a frame old: the predictor steps on
    ///        from the previous frame's snapshot, which shifts a 0.5 s target by a frame's time.
    bool FacingAt(RE::FormID actor, float seconds, float& yaw);

    /// @brief Whether the movement model runs (char_movement_model).
    bool On();

}  // namespace CB::feature::controller::move
