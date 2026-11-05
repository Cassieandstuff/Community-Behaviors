#pragma once

#include "CharFacing.h"

#include <locomotion/PoseSearch.h>

#include <array>
#include <string>

/// @file
/// @brief Motion matching: the character's side, which sets the graph variables CBPoseSearchGenerator reads, and the node's
///        side, its per-character run and its database.
/// @note  The predictor's query comes from the physics thread; the graph variables are written in the per-frame movement
///        hook (the update thread), where the engine itself hands the graph its `Speed`; the node reads them on the job
///        updating its graph.

class hkbContext;

namespace CB::feature::controller::motion {

    class PoseSearchGenerator;

    /// @brief The predictor's query: where the character will be and face when the clip playing at Rate() reaches the
    ///        database's horizons (engine::anim::features::kHorizons / rate, real seconds), and where they were and
    ///        faced that long ago, in the current root space (x right, y forward), game units.
    struct Query {
        CB::engine::locomotion::QueryTrajectory samples{};
        /// @brief The request (the stick at its speed cap) in world space, game units/s: a jump in it forces a search.
        float desiredX = 0, desiredY = 0;
    };

    /// @brief Whether motion matching runs (char_motion_matching); the predictor builds no query otherwise.
    bool On();

    /// @brief The playback rate for the movement model's latest frame, and the rate the clips play at from now on: the
    ///        asked-for speed over the clips' own speed for the gait, clamped to [0.7, 1.5]; null (no frame), no gait,
    ///        standing, or a gait no clip has keeps the last rate (1 at first).
    /// @note  Why the clips' gait speed and not the speed alone: they were authored slower than Skyrim moves (run 284
    ///        against 370), so the raw speed matched sprint clips at a run. Played at this rate they cover the ground
    ///        the controller covers, and the query samples the prediction at horizon / rate to compare like with like.
    /// @param dt the step's seconds: the rate eases toward its target over kRateHalflife rather than stepping, so a gait change
    ///        moves the clip speed and the query's sample times together and smoothly.
    float Rate(RE::FormID actor, const FrameSpeed* frame, float dt);

    /// @brief `actor`'s latest query, which the frame hook writes to its graph's variables.
    /// @note  Called by the predictor each step.
    void Publish(RE::FormID actor, const Query& q);
    /// @brief The predictor has no query for `actor` this step: the node searches nothing until the next.
    void PublishNoQuery(RE::FormID actor);

    /// @brief One update of `context`'s character's matcher for `node`: reads its graph's variables, searches when one is
    ///        due (every 0.1 s, at once near a clip's end, on a sharp change of the request, or on an interrupt) and writes
    ///        what plays next to the variables the graph binds.
    /// @param restart forget what played: the graph is coming back to the node.
    /// @return the child that plays next, -1 when the matcher has none (no database yet, no query yet, or no actor)
    int RunMatcher(const ::hkbContext& context, PoseSearchGenerator& node, float timestep, bool restart);

    /// @return the database `spec` states, null until it is built; the first request queues its build
    /// @note  Thread-safe. Equal selectors share one database, whichever graphs and characters they come from.
    const CB::engine::locomotion::Database* DatabaseFor(const CB::engine::locomotion::DatabaseSpec& spec);

    /// @brief SetTimeToLand's value when the predicted path doesn't land within its horizon (GASP's open upper bound).
    inline constexpr float kNoLanding = 999.0f;

    /// @brief The predictor's landing time for the chooser's MM_TimeToLand: seconds until its predicted path is first
    ///        grounded, kNoLanding when it isn't within the horizon. Read only while `actor` is airborne (0 on the
    ///        ground), so the predictor may publish it every step.
    /// @note  Thread-safe: the predictor (physics thread) writes it, the frame hook (update thread) reads it.
    void SetTimeToLand(RE::FormID actor, float seconds);

    /// @brief The player's motion matching state for a diagnostic line: the clip, its database and frame playing, the
    ///        dead blend's time left and the seconds since the last switch.
    std::string Describe();

}  // namespace CB::feature::controller::motion
