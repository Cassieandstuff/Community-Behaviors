#pragma once

#include <core/hooks/points/HookPoint.h>

namespace CB::core::hooks {

/// @brief One frame's movement as an actor's movement controller computes it from its agents.
/// @note Read by Actor_ApplyFrameMovement (0x1406698d0 AE): `speed` (game units/s) goes to the graph's `Speed`
///       (MiddleHighProcessData::currentMovementSpeed) AND, times the unit direction built from `angles` and dt,
///       becomes the translation handed to Actor::Move → bhkCharacterController::UpdatePhysics. One number feeds
///       both animation and physics. `angles[2]` goes to the process at +0xb8; the other fields are unread.
struct MovementFrame {
    float angles[3];
    float speed;
    float turn[3];
};
static_assert(sizeof(MovementFrame) == 0x1c);

/// @brief MovementControllerNPC's frame-movement computation (vtable +0x38): every agent's update, then the
///        handler, into `out`.
/// @note An Entry seam: it runs for every actor (the player's controller is a MovementControllerNPC too, built by
///       Actor::CreateMovementController), so subscribers filter by controller. Actor_ApplyFrameMovement calls it in
///       the default code-driven mode only (not ragdoll, not mounted in the ridden state, not when graph
///       bIsSynced takes the animation delta).
struct MovementControllerFrame : HookPoint<MovementControllerFrame, void(void* controller, float deltaTime, MovementFrame* out)> {
    static constexpr const char*           name   = "MovementControllerFrame";
    static constexpr Seam                  seam   = Seam::Entry;
    static constexpr const engine::Symbol& symbol = engine::MovementControllerNPC_ComputeFrameMovement;
};

}  // namespace CB::core::hooks
