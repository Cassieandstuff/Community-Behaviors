#pragma once

#include <cmath>
#include <cstdint>

/// @file
/// @brief The player's frame driver (CharFacing): each frame it steps the movement model (CharMovementModel) and hands
///        the engine its velocity and facing. Shared vocabulary for the model, the predictor and motion matching.
/// @note It drives the player while CB's camera drives the third-person camera (features/Camera): the stick is read
///       relative to the camera's yaw, and the move input is rewritten to forward-only while the engine computes.

namespace CB::feature::controller {

    /// @brief The heading the player's stick asks for this frame, in Skyrim's convention (angle.z: radians clockwise
    ///        from +y): the raw stick made relative to CB's camera.
    /// @return false without one: CB's camera not driving third person, no frame within the last half second, or the
    ///         stick idle. Thread-safe.
    bool StickHeading(float& heading);

    /// @brief The actor's gait (ActorState: sprinting before running before walking); None while none is set.
    enum class Gait : std::uint8_t { None, Walk, Run, Sprint };

    /// @brief The latest frame's speeds (game units/s): the forward speed the engine asked for (`full`) and the speed
    ///        the movement model handed on (`speed`).
    struct FrameSpeed {
        float full  = 0.0f;
        float speed = 0.0f;
        float slope = 1.0f;   ///< the smoothed slope factor (SlopeSpeedScale) in the cap
        Gait  gait  = Gait::None;
    };
    /// @brief False while the movement model is off or no frame ran within the last half second. Thread-safe.
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
