#pragma once

#include <cmath>

/// @file
/// @brief The Advanced Controller's facing model, shared by the yaw-control feature (CharFacing) and the predictor so
///        the facing the player gets and the facing that is predicted are the same model.
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

    /// @brief `a` wrapped into (-pi, pi].
    inline float WrapPi(float a)
    {
        constexpr float kPi = 3.14159265f, kTwoPi = 6.2831853f;
        a = std::fmod(a + kPi, kTwoPi);
        if (a < 0.0f) a += kTwoPi;
        return a - kPi;
    }

}  // namespace CB::feature::controller
