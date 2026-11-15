#pragma once

#include <hky/relay/Stairs.h>

#include <cstdint>

/// @file
/// @brief The ground's material per character, as the movement rules' surface functions read it (IsOnMaterial,
///        IsAheadMaterial and their groups, HKY-SDK relay/Surface.h): what the floor query stands the character on and
///        what its predicted path crosses.
/// @note  Written by the trajectory predictor each physics step it runs for a managed character (the player every step,
///        an NPC at its prediction rate); read from any thread that evaluates a condition.

namespace CB::feature::controller::surface {

    /// @brief Records the material under `actor`'s feet this step (a MATERIAL_ID, 0 when airborne or unknown).
    void SetNow(std::uint32_t actor, std::uint32_t material);

    /// @brief Records the material ahead of `actor`: the one most of its predicted path's grounded points stand on
    ///        (0 when it predicts no movement).
    void SetAhead(std::uint32_t actor, std::uint32_t material);

    /// @brief Records `actor`'s stairs (CharStairs, held), as the relay's stairs functions read them.
    void SetStairs(std::uint32_t actor, const hky::relay::StairsReading& stairs);

    /// @return whether `actor` has a fresh stairs reading, into `out`
    bool Stairs(std::uint32_t actor, hky::relay::StairsReading& out);

    /// @brief Makes these readings the relay's surface and stairs sources (relay::SetSurfaceSource,
    ///        relay::SetStairsSource). Idempotent.
    void Bind();

}  // namespace CB::feature::controller::surface
