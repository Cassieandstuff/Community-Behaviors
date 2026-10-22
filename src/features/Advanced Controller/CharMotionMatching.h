#pragma once

#include "CharFacing.h"

#include <array>
#include <string>

/// @file
/// @brief Motion matching for the player (the stock-node prototype): the predictor's trajectory plus the current pose
///        searched against the Motion Matching add-on's clips, the winner played through the add-on's graph.
/// @note  The search runs where the predictor runs (the physics thread); the graph variables are written in the
///        per-frame movement hook (the update thread), where the engine itself hands the graph its `Speed`.

namespace CB::feature::controller::motion {

    /// @brief The predictor's query: where the player will be and face when the clip playing at Rate() reaches the
    ///        database's horizons (engine::anim::features::kHorizons / rate, real seconds), and where they were and
    ///        faced that long ago, in the current root space (x right, y forward), game units.
    struct Query {
        std::array<float, 3> x{}, y{}, dirX{}, dirY{};
        std::array<float, 3> pastX{}, pastY{}, pastDirX{}, pastDirY{};
        /// @brief The request (stick and speed ramp target) in world space, game units/s: a jump in it forces a search.
        float desiredX = 0, desiredY = 0;
        Gait  gait = Gait::None;   ///< the controller's gait (logged beside each pick's frame gait)
    };

    /// @brief Whether motion matching runs (char_motion_matching); the predictor builds no query otherwise.
    bool On();

    /// @brief The playback rate for the speed ramp's latest frame, and the rate the clips play at from now on: the
    ///        asked-for speed over the clips' own speed for the gait, clamped to [0.7, 1.5]; null (no frame), no gait,
    ///        standing, or a gait no clip has keeps the last rate (1 at first).
    /// @note  Why the clips' gait speed and not the speed alone: they were authored slower than Skyrim moves (run 284
    ///        against 370), so the raw speed matched sprint clips at a run. Played at this rate they cover the ground
    ///        the controller covers, and the query samples the prediction at horizon / rate to compare like with like.
    float Rate(const FrameSpeed* frame);

    /// @brief Search with `q` when a search is due (every 0.1 s, or at once near a clip's end).
    /// @note  Called by the predictor each step; does nothing until the database is built.
    void Search(const Query& q);

    /// @brief The player's motion matching state for a diagnostic line: the clip and frame playing, the dead blend's
    ///        time left and the seconds since the last switch.
    std::string Describe();

}  // namespace CB::feature::controller::motion
