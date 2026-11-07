#include <locomotion/Dodge.h>

#include <locomotion/Movement.h>

#include <algorithm>
#include <cmath>

namespace CB::engine::locomotion {

    namespace {

        constexpr float kSector = 3.14159265f / 4.0f;

        /// @brief The 8-way sector (0 = forward, clockwise) of a heading relative to the body.
        int SectorOf(float relative)
        {
            const int s = static_cast<int>(std::floor(relative / kSector + 0.5f));
            return ((s % 8) + 8) % 8;
        }

    }  // namespace

    DodgeDirectionPick PickDodgeDirection(bool haveDirection, float worldHeading, float bodyHeading)
    {
        if (!haveDirection) return {};
        const float relative = WrapPi(worldHeading - bodyHeading);
        return { static_cast<DodgeDirection>(1 + SectorOf(relative)), relative };
    }

    TapHold::Result TapHold::Step(bool down, double now, float maxTap)
    {
        Result r;
        if (down && !down_) {
            down_   = true;
            downAt_ = now;
        } else if (!down && down_) {
            down_    = false;
            r.tapped = now - downAt_ <= maxTap;
        }
        r.holding = down_ && now - downAt_ > maxTap;
        return r;
    }

    bool DoubleTap::Step(float x, float y, double now, float window)
    {
        const float len = std::sqrt(x * x + y * y);
        if (pushed_) {
            if (len < kRelease) pushed_ = false;
            return false;
        }
        if (len < kPush) return false;
        pushed_      = true;
        const int s  = SectorOf(std::atan2(x, y));
        if (s == sector_ && now - startAt_ <= window) {
            sector_ = -1;
            return true;
        }
        sector_  = s;
        startAt_ = now;
        return false;
    }

    float DodgeStaminaCost(const DodgeStamina& s, float maxStamina, float mult)
    {
        const float base = s.mode == DodgeCostMode::PercentOfMax ? s.base * 0.01f * maxStamina : s.base;
        return (std::max)(0.0f, base * mult);
    }

    bool DodgeAffordable(const DodgeStamina& s, float current, float cost)
    {
        return s.requireFull ? current >= cost : current > 0.0f || cost <= 0.0f;
    }

}  // namespace CB::engine::locomotion
