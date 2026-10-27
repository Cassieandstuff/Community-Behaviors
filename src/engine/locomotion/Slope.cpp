#include <locomotion/Slope.h>

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

    void SlideStep(const Surface& g, float gx, float gy, float gz, float& sx, float& sy, float dt)
    {
        /// @brief The slide never exceeds this (Havok units/s, about 1050 game units/s); the old Sliding state's cap.
        constexpr float kMaxSlideSpeed = 15.0f;
        /// @brief On fully supporting ground the slide halves in this long (seconds).
        constexpr float kSlideBleedHalflife = 0.1f;
        if (!g.grounded) return;
        const float pull = 1.0f - g.support;
        const float gn   = gx * g.nx + gy * g.ny + gz * g.nz;
        const float px = gx - gn * g.nx, py = gy - gn * g.ny, pz = gz - gn * g.nz;
        const float ph = std::sqrt(px * px + py * py);
        if (pull > 0.0f && ph > 1.0e-6f) {
            const float a = std::sqrt(px * px + py * py + pz * pz) * pull * dt / ph;
            sx += px * a, sy += py * a;
        }
        const float keep = std::exp(-0.6931472f / kSlideBleedHalflife * g.support * dt);
        sx *= keep, sy *= keep;
        const float len = std::sqrt(sx * sx + sy * sy);
        if (len > kMaxSlideSpeed) sx *= kMaxSlideSpeed / len, sy *= kMaxSlideSpeed / len;
    }

    void ClipUphill(const Surface& g, float& dx, float& dy)
    {
        if (!g.grounded || g.support >= 1.0f) return;
        const float h = std::sqrt(g.nx * g.nx + g.ny * g.ny);
        if (h < 1.0e-6f) return;
        const float ux = -g.nx / h, uy = -g.ny / h;   ///< uphill: the normal leans downhill
        const float up = dx * ux + dy * uy;
        if (up <= 0.0f) return;
        const float cut = (1.0f - g.support) * up;
        dx -= cut * ux, dy -= cut * uy;
    }

}  // namespace CB::engine::locomotion
