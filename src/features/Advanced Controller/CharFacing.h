#pragma once

#include <locomotion/Movement.h>

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

    using CB::engine::locomotion::Gait;

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

    using CB::engine::locomotion::WrapPi;

}  // namespace CB::feature::controller
