#pragma once

#include <PCH.h>

/// @file
/// @brief Game functions of actor movement, declared so features can call them and Override<&fn> them.
/// @note Free functions with the object first: the classes are CommonLib's (RE::), which this does not re-declare.

namespace CB::platform::game {

    /// @brief One frame's movement as an actor's movement controller computes it from its agents.
    /// @note Read by Actor_ApplyFrameMovement (0x1406698d0 AE): `speed` (game units/s) goes to the graph's `Speed`
    ///       (MiddleHighProcessData::currentMovementSpeed) AND, times the unit direction built from `angles` and dt,
    ///       becomes the translation handed to Actor::Move → bhkCharacterController::UpdatePhysics. One number feeds
    ///       both animation and physics. `angles[2]` goes to the process at +0xb8. `turn` is a rate (rad/s): in an
    ///       attached cell it calls Actor::SetHeading(heading + turn[2] * dt) — skipped for the player while
    ///       PlayerCharacter+0xbe5 bit 2 is set — then turn[0] (and turn[1]) go to the pitch or to the character
    ///       controller, all before Actor::Move takes the translation, so the move sees the new heading.
    struct MovementFrame {
        float angles[3];
        float speed;
        float turn[3];
    };
    static_assert(sizeof(MovementFrame) == 0x1c);

    /// @brief MovementControllerNPC's frame-movement computation (vtable +0x38): every agent's update, then the
    ///        handler, into `out`.
    /// @note Runs for every actor (the player's controller is a MovementControllerNPC too, built by
    ///       Actor::CreateMovementController), so an override filters by controller. Actor_ApplyFrameMovement calls it
    ///       in the default code-driven mode only (not ragdoll, not mounted in the ridden state, not when graph
    ///       bIsSynced takes the animation delta).
    void MovementControllerNPC_ComputeFrameMovement(RE::MovementControllerNPC* controller, float deltaTime, MovementFrame* out);

}  // namespace CB::platform::game
