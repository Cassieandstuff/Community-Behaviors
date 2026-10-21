#pragma once

#include "CharGround.h"

#include <array>
#include <cstdint>

/// @file
/// @brief The character debug HUD's data: each controller feature publishes its part of the player's step here, and
///        the HUD (char_debug_hud) draws the latest copy over the finished frame.
/// @note Positions and velocities are Havok world units (the HUD converts); publishing is a no-op unless the HUD
///       flag is on, so the features pay nothing otherwise.

namespace CB::feature::controller::hud {

    struct Point {
        float x = 0, y = 0, z = 0;
    };

    /// @brief One floor-query ray as cast: from `top` straight down to `bottom`, and what it hit.
    struct Ray {
        Point top, bottom;
        bool  hit      = false;
        float hitZ     = 0;
        bool  walkable = false;
        bool  wall     = false;   ///< steeper than wallNormalZ, or another character: never support
    };

    /// @brief The ground states' step: what they decided and the motion they handed the proxy.
    struct Controller {
        Point               feet;
        float               radius = 0, height = 0;
        const char*         mode   = "-";      ///< Grounded / Airborne (static strings)
        const char*         entry  = "-";      ///< how the last airborne phase began
        Ground              verdict = Ground::Airborne;
        float               groundNz = 1;
        int                 supportHits = 0, walkableHits = 0;
        float               support = 0, airTime = 0;   ///< the floor verdict's support (0-1)
        Point               velocity, desired;   ///< the step's output velocity and the movement asked for
        Point               slide;               ///< the slide velocity (horizontal), part of `velocity`
        bool                variableJump = false, jumpReleased = false;
        float               jumpIn = -1;         ///< seconds left on a pending jump's wind-up, -1 for none
        std::array<Ray, 9>  rays{};
    };

    /// @brief The predictor's trajectory (P1) and the path through it, in world space.
    struct Prediction {
        static constexpr int kSamples = 3;
        static constexpr int kPath    = 12;
        std::array<Point, kSamples>  at{};
        std::array<Ground, kSamples> ground{};
        std::array<float, kSamples>  support{};
        std::array<float, kSamples>  time{};
        std::array<Point, kPath>     path{};         ///< the predicted path's points (falls and jumps included)
        std::array<Ground, kPath>    pathGround{};
        std::array<float, kPath>     pathSupport{};
        int          pathCount = 0;
        bool         blocked   = false;
        Point        blockedAt;                     ///< where the path stops at an impassable hit
        float        blockedIn = -1.0f;             ///< seconds to that hit, -1 for none
        std::int32_t iState    = 0;
    };

    /// @brief The speed ramp's frame (game units/s): the speed handed on, the speed asked for, the turn damping's
    ///        fraction (TurnSpeedScale) and the smoothed slope factor (SlopeSpeedScale).
    struct Ramp {
        float speed = 0, target = 0, turn = 1, slope = 1;
    };

    /// @brief Motion matching's last search: the clip and frame it chose, that frame's cost, the search's time and the
    ///        switches so far.
    struct Matching {
        char  clip[48] = {};
        int   frame    = 0;
        float cost = 0, searchMs = 0;
        int   switches = 0;
    };

    /// @brief Whether the HUD runs (char_debug_hud); publishers skip their work when it does not.
    bool On();

    void Publish(const Controller& c);
    void Publish(const Prediction& p);
    void Publish(const Ramp& r);
    void Publish(const Matching& m);

}  // namespace CB::feature::controller::hud
