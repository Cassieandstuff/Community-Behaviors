#pragma once

#include "CharFacing.h"

#include <hky/locomotion/PoseSearch.h>

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

    /// @brief The predictor's query: where the character will be and face at the database's sample times ahead
    ///        (engine::anim::features::kAhead, real seconds), and where they were and faced that long ago (kBack), in the
    ///        current root space (x right, y forward), game units.
    struct Query {
        hky::locomotion::QueryTrajectory samples{};
        /// @brief The request (the stick at its speed cap) in world space, game units/s: a jump in it forces a search.
        float desiredX = 0, desiredY = 0;
    };

    /// @brief Whether motion matching runs (char_motion_matching); the predictor builds no query otherwise.
    bool On();

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

    /// @return the database `spec` states, measured through `rig`, null until it is built; the first request queues its build
    /// @note  Thread-safe. Equal selectors over equal rigs share one database, whichever graphs and characters they come from.
    const hky::locomotion::Database* DatabaseFor(const hky::locomotion::DatabaseSpec& spec,
                                                        const hky::anim::Rig& rig = {});

    /// @brief SetTimeToLand's value when the predicted path doesn't land within its horizon (GASP's open upper bound).
    inline constexpr float kNoLanding = 999.0f;

    /// @brief The predictor's landing time for the chooser's MM_TimeToLand: seconds until its predicted path is first
    ///        grounded, kNoLanding when it isn't within the horizon. Read only while `actor` is airborne (0 on the
    ///        ground), so the predictor may publish it every step.
    /// @note  Thread-safe: the predictor (physics thread) writes it, the frame hook (update thread) reads it.
    void SetTimeToLand(RE::FormID actor, float seconds);

    /// @brief What distance matching reads of a character, as of its latest movement frame.
    /// @brief The movement model's distances distance matching plays by (the engine's).
    using Distances = hky::locomotion::Distances;

    /// @brief `actor`'s distances from the movement model (locomotion::PredictStopDistance, PredictPivotDistance).
    /// @return false when no movement frame has published them (CB does not drive the actor)
    /// @note  Thread-safe: the frame hook writes them, graph nodes read them. Also written to the graph variables
    ///        MM_DistanceToStop and MM_DistanceToPivot where a graph declares them.
    bool DistancesOf(RE::FormID actor, Distances& out);

    /// @brief The distance curve of the clip `animationName` (byte-exact, below the character folder) from `marker`,
    ///        measured from its animation unit on first use and kept.
    /// @return null when CB ships no unit for it, or before the load order is read (asked again later)
    /// @note  Thread-safe. The first ask for a clip measures it on the asking thread (a few milliseconds).
    std::shared_ptr<const hky::locomotion::DistanceCurve> DistanceCurveOf(const std::string& animationName,
                                                                                 hky::locomotion::DistanceMarker marker);

    /// @brief The turn the playing clip's root made since the last call, while that clip is from a database tagged
    ///        TurnInPlace (PoseDatabase::rootTurns), or with `anyDatabase` any clip with a root yaw (the body facing,
    ///        locomotion::StepBody): the clip turns the character as it turns its feet.
    /// @param delta radians, the actor heading's convention (increasing turns right); 0 on the first frame of a clip, after
    ///        a jump in its time, or when the call is the first since the node last played
    /// @return whether such a clip plays now (the node published it within the last kRootTurnStale): the caller's facing
    ///         spring then waits
    /// @param ahead when given, radians the clip's root turns over the next `aheadSeconds`, read up to the clip's end: the
    ///        turn steering scales (locomotion::BodyFacingInput::clipTurnWindow, GASP's AnimatedTargetTime)
    /// @param turnInPlace when given, whether the clip's database is tagged TurnInPlace (PoseDatabase::rootTurns): GASP's
    ///        second Steering node scales its turn even standing
    /// @note  The frame hook's alone (one caller per actor, one mode per frame): it keeps what it read last.
    bool RootTurn(RE::FormID actor, float& delta, bool anyDatabase = false, float* ahead = nullptr, float aheadSeconds = 2.0f,
                  bool* turnInPlace = nullptr);

    /// @brief The player's motion matching state for a diagnostic line: the clip, its database and frame playing, the
    ///        dead blend's time left and the seconds since the last switch.
    std::string Describe();

}  // namespace CB::feature::controller::motion
