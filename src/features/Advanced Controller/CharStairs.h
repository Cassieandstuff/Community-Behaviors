#pragma once

#include "CharDebugHud.h"
#include "CharGround.h"

#include <cstdint>
#include <span>

/// @file
/// @brief Stairs from collision topology in the game: HKY-SDK's StairsDetector run for each managed character along its
///        predicted path, held per actor in the surface table (CharSurface) where the relay's stairs functions
///        (IsOnStairs, IsAheadStairs, StairsDirection...) and MM_StairsRise read it.
/// @note  Runs from the trajectory predictor's step, so an NPC detects at its prediction rate (fNpcPredictNear /
///        fNpcPredictEvery), and only while asked: a live movement rule calls a stairs function, or the survey
///        (char_stairs_survey) is on for the player.

namespace CB::feature::controller::stairs {

    /// @brief Whether any live movement rule's condition calls a stairs function; CharMoveRules sets it on each load.
    void SetRulesAsk(bool ask);

    /// @return whether the detector runs for this character (`player`: the player's own, which the survey also asks for)
    bool Wanted(bool player);

    /// @brief What one step's detection reads from the predictor.
    struct Step {
        RE::FormID                      actor = 0;
        bool                            player = false;
        std::span<const hud::Point>     path;          ///< the predicted feet, Havok world units, nearest first
        float                           dirX = 0.0f, dirY = 0.0f;   ///< without a path: the facing, unit, horizontal
        float                           speed = 0.0f;  ///< horizontal, Havok units/s
        std::uint64_t                   ignoreLayers = 0;   ///< the predictor's sweep layers (characters, sensors)
    };

    /// @brief Marches and reads `ctrl`'s character's stairs this step and records the held reading for the relay.
    /// @note  On the thread stepping `ctrl`; only it touches this character's detector.
    void Update(::bhkCharacterController& ctrl, const Hull& hull, const Step& step);

    /// @return `actor`'s riser height signed by direction (+ up) while on a flight, else 0: MM_StairsRise
    float SignedRise(RE::FormID actor);

}  // namespace CB::feature::controller::stairs
