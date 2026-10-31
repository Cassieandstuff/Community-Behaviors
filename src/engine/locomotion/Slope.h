#pragma once

#include <cmath>

/// @file
/// @brief The ground's pull on a character: how a slope rises along a move, changes the run speed, drags (the slide),
///        clips an uphill request and shapes a jump's launch. Pure math over the ground under the feet, which the
///        caller measures (the game: its floor query's verdict).
/// @note  Havok units (1 per 70 game units), z up, unlike the movement step's game units.

namespace CB::engine::locomotion {

    /// @brief The ground under the feet, as the slope helpers read it.
    struct Surface {
        bool  grounded = false;                  ///< false: airborne, every helper leaves the character alone
        float support  = 0.0f;                   ///< 0-1: how much the ground holds the character up (0 when airborne)
        float nx = 0.0f, ny = 0.0f, nz = 1.0f;   ///< the ground's normal, unit length
    };

    /// @brief The numbers the game reads from settings.ini [Character] (the plugin fills it; the defaults are its).
    struct Tuning {
        float slopeSpeedDown = 1.0f;   ///< fSlopeSpeedDown: downhill speed gain per sin(slope), at least 0
        float slopeSpeedUp   = 0.5f;   ///< fSlopeSpeedUp: uphill speed loss per sin(slope), at least 0
    };

    /// @brief How far the ground `g` rises along a horizontal move (dx, dy) if it keeps its plane (Havok units); with a
    ///        velocity, the vertical speed that following the ground gives.
    /// @note Clamped to a 60 deg grade: a near-vertical normal would extrapolate the move into a cliff. 0 when airborne.
    inline float SlopeRise(const Surface& g, float dx, float dy)
    {
        if (!g.grounded || g.nz < 0.1f) return 0.0f;
        const float rise  = -(g.nx * dx + g.ny * dy) / g.nz;
        const float limit = 1.7320508f * std::sqrt(dx * dx + dy * dy);
        return rise < -limit ? -limit : rise > limit ? limit : rise;
    }

    /// @brief The halflife (seconds) of the smoothing on SlopeSpeedScale: a change settles in about 0.3 s, so cresting
    ///        a hill eases the speed down instead of snapping it.
    constexpr float kSlopeSpeedHalflife = 0.1f;

    /// @brief How much the slope along a heading (dirX, dirY, horizontal, unit length) changes the run speed: gravity's
    ///        share along the run. Downhill 1 + slopeSpeedDown x sin(slope) up to 1.5; uphill 1 - slopeSpeedUp x
    ///        sin(slope) down to 0.7; 1 in the air.
    /// @note Gravity along the slope is split by support: the share the ground holds (support) changes how fast the
    ///       character runs (this), the share it does not (1 - support) drags them (SlideStep). So on ground too steep to
    ///       stand on the speed change fades out and the slide takes over, and gravity is never counted twice.
    float SlopeSpeedScale(const Surface& g, float dirX, float dirY, const Tuning& tuning);

    /// @brief The slide: a horizontal velocity the character does not control, added to its own on the ground. It gains
    ///        gravity's pull along the ground scaled by 1 - support, bleeds off at a rate scaled by support, and never
    ///        exceeds kMaxSlideSpeed.
    /// @param gx gravity (Havok units/s^2, already scaled by the controller's gravity factor).
    /// @note The gain is horizontal at gravity's along-slope magnitude because the ground step lays the horizontal
    ///       velocity onto the plane at the same speed, so the body accelerates down the fall line at g sin(slope)
    ///       times 1 - support.
    void SlideStep(const Surface& g, float gx, float gy, float gz, float& sx, float& sy, float dt);

    /// @brief Removes 1 - support of the uphill part of a horizontal request: on ground that does not hold the
    ///        character, it can steer across and down it but not climb it (Jolt's CharacterVirtual sample does the same
    ///        on steep ground).
    void ClipUphill(const Surface& g, float& dx, float& dy);

    /// @brief A jump's launch velocity from ground `g` at horizontal ground velocity (hx, hy) and jump speed `speed`
    ///        (Havok units/s): the whole ground velocity (it lies along the slope, SlopeRise its vertical part) plus the
    ///        push off the ground, along the ground's normal with the part that leans against the run removed.
    /// @note One rule for every slope: uphill the normal leans back against the run, so the push is straight up and the
    ///       run's uphill momentum carries the jump over the rising ground; downhill it leans with the run, so the push
    ///       follows the normal (Sonic's jump) on top of the slope's own velocity; standing, crossing a slope or on the
    ///       flat it is straight up. Continuous through flat ground: only the normal's lean along the run counts, and
    ///       only when positive.
    /// @note Why the momentum: vanilla's launch keeps only the horizontal speed, so a jump up a slope lost the run's
    ///       upward motion and met the rising ground early. Pushing up along the full normal instead braked the run.
    inline void JumpLaunch(const Surface& g, float hx, float hy, float speed, float& ox, float& oy, float& oz)
    {
        float       dx = 0.0f, dy = 0.0f, dz = 1.0f;
        const float h  = std::sqrt(hx * hx + hy * hy);
        if (g.grounded && h > 1.0e-4f) {
            const float ux = hx / h, uy = hy / h;
            const float lean = g.nx * ux + g.ny * uy;   ///< the normal's lean along the run, + downhill
            if (lean > 0.0f) {
                dx = ux * lean, dy = uy * lean, dz = g.nz;
                const float l = std::sqrt(dx * dx + dy * dy + dz * dz);
                dx /= l, dy /= l, dz /= l;
            }
        }
        ox = hx + speed * dx;
        oy = hy + speed * dy;
        oz = SlopeRise(g, hx, hy) + speed * dz;
    }

}  // namespace CB::engine::locomotion
