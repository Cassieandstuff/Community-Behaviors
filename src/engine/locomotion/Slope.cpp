#include <locomotion/Slope.h>

#include <locomotion/Pogo.h>

#include <algorithm>
#include <cmath>

namespace CB::engine::locomotion {

    float SlopeSpeedScale(const Surface& g, float dirX, float dirY, const Tuning& tuning)
    {
        if (!g.grounded) return 1.0f;
        const float down  = (std::max)(0.0f, tuning.slopeSpeedDown);
        const float up    = (std::max)(0.0f, tuning.slopeSpeedUp);
        const float rise  = SlopeRise(g, dirX, dirY);   ///< tan of the slope along the heading, + uphill
        const float sine  = rise / std::sqrt(1.0f + rise * rise);
        const float scale = sine < 0.0f ? (std::min)(1.5f, 1.0f - down * sine) : (std::max)(0.7f, 1.0f - up * sine);
        return 1.0f + std::clamp(g.support, 0.0f, 1.0f) * (scale - 1.0f);
    }

    void GripStep(const Surface& g, float grip, float gripAccel, float gx, float gy, float gz, float dx, float dy, float& vx, float& vy,
                  float& ax, float& ay, float& sx, float& sy, float dt)
    {
        /// @brief The slide never exceeds this (Havok units/s, about 1050 game units/s); the old Sliding state's cap.
        constexpr float kMaxSlideSpeed = 15.0f;
        if (!g.grounded || !(dt > 0.0f)) return;
        const float gn = gx * g.nx + gy * g.ny + gz * g.nz;
        const float px = gx - gn * g.nx, py = gy - gn * g.ny, pz = gz - gn * g.nz;
        const float ph = std::sqrt(px * px + py * py);
        if (ph > 1.0e-6f) {
            const float a = std::sqrt(px * px + py * py + pz * pz) * dt / ph;
            sx += px * a, sy += py * a;
        }
        float       budget = std::clamp(grip, 0.0f, 1.0f) * (std::max)(0.0f, gripAccel) * dt;
        const float slide  = std::sqrt(sx * sx + sy * sy);
        if (slide > 1.0e-6f) {
            const float cancel = (std::min)(slide, budget);
            const float keep   = (slide - cancel) / slide;
            sx *= keep, sy *= keep;
            budget -= cancel;
        }
        const float ox = vx, oy = vy;
        GroundVelocityStep(vx, vy, ax, ay, dx, dy, dt);
        const float cx = vx - ox, cy = vy - oy;
        const float change = std::sqrt(cx * cx + cy * cy);
        if (change > budget) {
            const float k = change > 0.0f ? budget / change : 0.0f;
            vx = ox + cx * k, vy = oy + cy * k;
            ax *= k, ay *= k;
        }
        const float len = std::sqrt(sx * sx + sy * sy);
        if (len > kMaxSlideSpeed) sx *= kMaxSlideSpeed / len, sy *= kMaxSlideSpeed / len;
    }

}  // namespace CB::engine::locomotion
