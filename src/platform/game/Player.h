#pragma once

#include <PCH.h>

#include <cstdint>

/// @file
/// @brief Game functions of the player's sprint and jump, declared so features can call them and Override<&fn> them.
/// @note Free functions with the object first: the classes are CommonLib's (RE::), which this does not re-declare.
/// @note The sprint REQUEST is PlayerCharacter::GetPlayerFlags().isSprinting (+0xbe5 bit 0 on 1.6.1170, +0xbed on
///       1.7.104): SprintHandler::ProcessButton flips it on a press only (a toggle; it refuses a start at stamina <= 0),
///       nothing clears it on release. Whether the actor sprints is a separate thing: PlayerCharacter_IsSyncSprintState.

namespace CB::platform::game {

    /// @brief The default objects PlayerControls_DoAction runs (BGSDefaultObjectManager indices).
    constexpr std::int32_t kActionSprintStart = 0x41;
    constexpr std::int32_t kActionSprintStop  = 0x42;

    /// @brief The player's per-frame sprint evaluator: turns the request bit into SprintStart / SprintStop actions.
    /// @note Vanilla works on the controlling actor (Actor::GetControllingActor: the player, or the mount) and allows a
    ///       sprint iff the request bit && (sprinting || IsRunning || IsBlocking) && not falling hard (in air, vertical
    ///       speed past a threshold) && !IsOverEncumbered && (sync-sprinting || not attacking) && PlayerControls
    ///       prevMoveVec inside a ~30 deg forward cone && stamina > 0. Any failure CLEARS the request bit. When the
    ///       verdict differs from PlayerCharacter_IsSyncSprintState it calls PlayerControls_DoAction(start/stop, 2).
    void PlayerCharacter_UpdateSprintRequest(RE::PlayerCharacter* player);

    /// @brief Whether `actor` sprints as its graph has it: graph int iSyncSprintState > 0.
    bool PlayerCharacter_IsSyncSprintState(RE::Actor* actor);

    /// @brief Runs the default-object action `defaultObject` (kActionSprintStart, ...) on the player.
    /// @return whether the action was taken.
    bool PlayerControls_DoAction(RE::PlayerControls* controls, std::int32_t defaultObject, std::int32_t priority);

    /// @brief The jump key's handler (vtable slot 4).
    /// @note Off a flying mount, on a press (value != 0, held 0): needs the character controller's kCanJump flag or a
    ///       mount, then calls Actor_Jump ONLY if !PlayerCharacter_IsSyncSprintState && !ActorState sprinting — the
    ///       one "no jump while sprinting" rule. A flying mount takes other branches.
    void JumpHandler_ProcessButton(RE::JumpHandler* handler, RE::ButtonEvent* event, RE::PlayerControlsData* data);

    /// @brief Makes `actor` jump: for the player, PlayerControls_DoAction(0x38, 2), then the controller's jump impulse.
    void Actor_Jump(RE::Actor* actor);

    /// @brief Per actor, from Actor::Update: while sprinting (not mounted; NPCs only as player teammates) damages
    ///        Stamina by Actor_SprintStaminaDrain; its tail syncs ActorState sprinting from a graph bool.
    /// @warning The tail must keep running: an override that skips `next` leaves the sprint flag out of sync.
    void Actor_UpdateSprint(RE::Actor* actor);

    /// @brief Stamina a sprint costs over `deltaTime` seconds:
    ///        (fSprintStaminaWeightMult * equippedWeight + fSprintStaminaWeightBase) * deltaTime * fSprintStaminaDrainMult.
    /// @note Its only caller is Actor_UpdateSprint.
    float Actor_SprintStaminaDrain(float equippedWeight, float deltaTime);

}  // namespace CB::platform::game
