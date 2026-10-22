#pragma once

#include <cmath>
#include <cstdint>

/// @file
/// @brief The Advanced Controller's frame-movement model, shared by the frame features (CharFacing turns the player,
///        CharSpeedRamp sets the speed) and the predictor, so the movement the player gets and the movement that is
///        predicted are the same model.
/// @note Facing comes through True Directional Movement's API: TDM keeps its free-rotation camera, camera compensation
///       and forward-only input rewrite, and CB owns the yaw while TDM is in its directional mode.

namespace CB::feature::controller {

    /// @brief P2: the facing spring's halflife (seconds).
    constexpr float kFacingHalflife = 0.15f;

    /// @brief The heading the player's stick asks for this frame, in Skyrim's convention (angle.z: radians clockwise
    ///        from +y): TDM's unaltered stick input made camera-relative.
    /// @return false without one: TDM absent, not in its directional mode, not in third person, or the stick idle.
    /// @note The camera's yaw is the player's heading plus the third-person camera's free-rotation offset (vanilla's
    ///       ThirdPersonState::UpdateRotation; TDM keeps free rotation on while directional).
    bool StickHeading(float& heading);

    /// @brief The facing spring as CharFacing left it after the latest frame (Skyrim headings, radians).
    struct FacingState {
        float yaw      = 0.0f;    ///< the heading CB set, unwrapped near the body's heading
        float rate     = 0.0f;    ///< radians/s, clockwise positive
        bool  haveGoal = false;   ///< the stick asks for a direction
        float goal     = 0.0f;    ///< the stick's heading (StickHeading), when haveGoal
    };
    /// @brief The latest frame's facing; false while CB does not hold the yaw (no TDM, not directional, bFacing off)
    ///        or no frame ran within the last half second.
    /// @note Thread-safe: the frame writes it, the predictor (physics) and the HUD (render) read it.
    bool CurrentFacing(FacingState& out);

    /// @brief The fraction of its speed the player keeps while the facing is `gap` radians from the stick's direction:
    ///        1 up to [Character] fTurnFreeAngle (degrees, default 45), easing to 0 at 180 degrees.
    /// @note Why: the facing spring turns the body while the velocity follows it at full speed, so a reversal never
    ///       lost speed. Slowed by the gap, a hard turn drops the speed and the speed ramp builds it back as the body
    ///       comes around. fTurnFreeAngle=180 turns it off.
    float TurnSpeedScale(float gap);

    /// @brief The actor's gait (ActorState: sprinting before running before walking); None while none is set.
    enum class Gait : std::uint8_t { None, Walk, Run, Sprint };

    /// @brief The speed ramp's latest frame (game units/s): the speed the movement controller asked for (`full`) and
    ///        the speed handed on after the turn damping and the ramp (`speed`).
    struct FrameSpeed {
        float full     = 0.0f;
        float speed    = 0.0f;
        float rampTime = 0.0f;   ///< fRampTime: a rise from 0 to `full` takes this long
        float slope    = 1.0f;   ///< the smoothed slope factor (SlopeSpeedScale) already in `speed`
        Gait  gait     = Gait::None;
    };
    /// @brief False while the speed ramp is off or no frame ran within the last half second. Thread-safe.
    bool CurrentSpeed(FrameSpeed& out);

    /// @brief `a` wrapped into (-pi, pi].
    inline float WrapPi(float a)
    {
        constexpr float kPi = 3.14159265f, kTwoPi = 6.2831853f;
        a = std::fmod(a + kPi, kTwoPi);
        if (a < 0.0f) a += kTwoPi;
        return a - kPi;
    }

}  // namespace CB::feature::controller
