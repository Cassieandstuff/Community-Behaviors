#pragma once

#include <cstdint>

/// @file
/// @brief Advanced Dodge's decisions, pure: which input asks for a dodge (a tap of a held button, a key, a double tap
///        of a direction), which of 8 directions + none it goes, and what it costs in stamina. The plugin feeds the
///        game's input and actor values and raises the result in the graph (features/Advanced Dodge).
/// @note  Headings in Skyrim's convention: radians clockwise from +y.

namespace CB::engine::locomotion {

    /// @brief What asks for a dodge (settings.ini [Dodge] iTrigger).
    enum class DodgeTrigger : std::uint8_t {
        SprintTap    = 0,   ///< a tap of Sprint dodges, a hold sprints (DodgeFramework's default)
        DedicatedKey = 1,   ///< a key of its own, on its press
        DoubleTap    = 2,   ///< two pushes of the move input the same way within a window
    };

    /// @brief The dodge's direction relative to the body: none, then the 8 ways clockwise from forward.
    /// @note  The values are the graph contract (`AD_Direction`): a selector indexes its clips by them.
    enum class DodgeDirection : std::uint8_t { N = 0, F = 1, FR = 2, R = 3, BR = 4, B = 5, BL = 6, L = 7, FL = 8 };

    struct DodgeDirectionPick {
        DodgeDirection direction = DodgeDirection::N;
        /// @brief The asked direction relative to the body's facing, radians clockwise in (-pi, pi]; 0 for N.
        /// @note  Exact, not snapped to the 8 ways: what is left over the chosen clip's way is for its root motion.
        float          angle     = 0.0f;
    };

    /// @brief The nearest of the 8 ways to `worldHeading` seen from `bodyHeading`; N when `haveDirection` is false.
    DodgeDirectionPick PickDodgeDirection(bool haveDirection, float worldHeading, float bodyHeading);

    /// @brief One button that does one thing on a tap and another while held (Sprint: tap dodges, hold sprints).
    /// @note  The tap is known only on release, so a tap acts that late: the price of sharing the button.
    class TapHold {
    public:
        struct Result {
            bool tapped  = false;   ///< released this step after at most `maxTap` seconds
            bool holding = false;   ///< down for longer than `maxTap`
        };

        /// @param now seconds on any steady clock, never decreasing.
        Result Step(bool down, double now, float maxTap);

    private:
        bool   down_   = false;
        double downAt_ = 0.0;
    };

    /// @brief A double tap of the move input (keys or a stick, x right, y forward): two pushes out of rest into the
    ///        same of 8 sectors, the second within `window` seconds of the first.
    /// @note  A push starts at length >= kPush and ends below kRelease (hysteresis, so stick noise is not a tap).
    ///        Only pushes from rest count: tapping a side while holding forward is not a double tap.
    class DoubleTap {
    public:
        static constexpr float kPush    = 0.5f;
        static constexpr float kRelease = 0.25f;

        /// @return true on the second push; then `x`/`y` are its direction (the input this step).
        bool Step(float x, float y, double now, float window);

    private:
        bool   pushed_  = false;
        int    sector_  = -1;
        double startAt_ = 0.0;
    };

    /// @brief How a dodge's stamina cost is stated (settings.ini [Dodge] iStaminaMode).
    enum class DodgeCostMode : std::uint8_t {
        Flat         = 0,   ///< `base` points
        PercentOfMax = 1,   ///< `base` percent of the actor's maximum stamina
    };

    struct DodgeStamina {
        DodgeCostMode mode        = DodgeCostMode::Flat;
        float         base        = 20.0f;   ///< DMCO's default cost
        bool          requireFull = true;    ///< false: any stamina left allows the dodge (Dark Souls), and it empties
    };

    /// @brief The cost of one dodge: the base as `s.mode` states it, times `mult` (the dodge's own weight), never negative.
    float DodgeStaminaCost(const DodgeStamina& s, float maxStamina, float mult);

    /// @brief Whether `current` stamina allows a dodge costing `cost`.
    bool DodgeAffordable(const DodgeStamina& s, float current, float cost);

}  // namespace CB::engine::locomotion
