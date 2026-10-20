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
        const char*         mode   = "-";      ///< Grounded / Sliding / Airborne (static strings)
        const char*         entry  = "-";      ///< how the last airborne phase began
        Ground              verdict = Ground::Airborne;
        float               groundNz = 1;
        int                 supportHits = 0, walkableHits = 0;
        float               steepTime = 0, airTime = 0;
        Point               velocity, desired;   ///< the step's output velocity and the movement asked for
        bool                variableJump = false, jumpReleased = false;
        std::array<Ray, 9>  rays{};
    };

    /// @brief The predictor's trajectory (P1), in world space.
    struct Prediction {
        static constexpr int kSamples = 3;
        std::array<Point, kSamples>  at{};
        std::array<Ground, kSamples> ground{};
        std::array<float, kSamples>  time{};
        float        blockedAt = -1.0f;   ///< seconds to the first impassable hit, -1 for none
        std::int32_t iState    = 0;
    };

    /// @brief The speed ramp's frame (game units/s).
    struct Ramp {
        float speed = 0, target = 0;
    };

    /// @brief Whether the HUD runs (char_debug_hud); publishers skip their work when it does not.
    bool On();

    void Publish(const Controller& c);
    void Publish(const Prediction& p);
    void Publish(const Ramp& r);

}  // namespace CB::feature::controller::hud
