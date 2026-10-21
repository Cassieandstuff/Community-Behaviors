#pragma once

#include <array>

/// @file
/// @brief Motion matching for the player (the stock-node prototype): the predictor's trajectory plus the current pose
///        searched against the Motion Matching add-on's clips, the winner played through the add-on's graph.
/// @note  The search runs where the predictor runs (the physics thread); the graph variables are written in the
///        per-frame movement hook (the update thread), where the engine itself hands the graph its `Speed`.

namespace CB::feature::controller::motion {

    /// @brief The predictor's query: where the player will be and face at the database's horizons
    ///        (engine::anim::features::kHorizons), in the current root space (x right, y forward), game units.
    struct Query {
        std::array<float, 3> x{}, y{}, dirX{}, dirY{};
    };

    /// @brief Whether motion matching runs (char_motion_matching); the predictor builds no query otherwise.
    bool On();

    /// @brief Search with `q` when a search is due (every 0.1 s, or at once near a clip's end).
    /// @note  Called by the predictor each step; does nothing until the database is built.
    void Search(const Query& q);

}  // namespace CB::feature::controller::motion
