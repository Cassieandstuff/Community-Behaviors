#pragma once

#include <PCH.h>

#include <platform/hooks/CallSite.h>

/// @file
/// @brief Game functions and call sites where an actor takes a hit, magic, stagger and health damage.
/// @note Free functions with the object first: the classes are CommonLib's (RE::), which this does not re-declare.

namespace CB::platform::game {

    /// @brief Subtracts `damage` from `actor`'s Health: every source of health damage funnels here (weapon and arrow
    ///        hits, hostile magic, reflected damage, falls, drowning and water damage).
    /// @note Scales `damage` by the difficulty multiplier first (with `dontAdjustDifficulty`, only when that lowers
    ///       it), applies nothing at <= 0. `aggressor` is null for falls, drowning and water damage.
    /// @return whether `actor` is dead afterwards.
    bool Actor_DoDamage(RE::Actor* actor, float damage, RE::Actor* aggressor, bool dontAdjustDifficulty);

    /// @brief Staggers `actor` by `magnitude`: callers are the hit handler (HitData stagger), StaggerEffect (magic) and
    ///        AIProcess::KnockExplosion.
    /// @note Skips ragdolled actors; runs the perk entries Mod Outgoing Stagger (aggressor) and Mod Incoming Stagger
    ///       (actor) on the magnitude, clamps it to 1 and does nothing at <= 0; otherwise sets the graph's
    ///       staggerMagnitude / staggerDirection and fires the Stagger action. `aggressor` may be null.
    void Actor_Stagger(RE::Actor* actor, float magnitude, RE::Actor* aggressor);

    /// @brief The weapon/arrow hit delivery's Actor::IsGhost(target) CALL: true returns before anything is built, so
    ///        the hit passes through (no HitData, impact, stagger, on-hit spell, block XP or damage).
    /// @note The redirect MaxsuIFrame uses; Actor::IsGhost itself serves the engine elsewhere and stays untouched.
    using WeaponHitIsGhostCall = hooks::CallSite<hooks::engine::WeaponHit_IsGhostCall, bool(const RE::Actor*)>;

    /// @brief Actor's MagicTarget::IsInvulnerable (vtable slot 4) calling Actor::IsGhost on its actor: true rejects
    ///        the magic.
    using ActorInvulnerableIsGhostCall = hooks::CallSite<hooks::engine::Actor_IsInvulnerable_IsGhostCall, bool(const RE::Actor*)>;

    /// @brief PlayerCharacter's MagicTarget::IsInvulnerable: god mode first, then Actor::IsGhost on the player.
    using PlayerInvulnerableIsGhostCall = hooks::CallSite<hooks::engine::PlayerCharacter_IsInvulnerable_IsGhostCall, bool(const RE::Actor*)>;

}  // namespace CB::platform::game
