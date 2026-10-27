#pragma once

/// @file
/// @brief CB's third-person camera (bCamera): what other features read from it.

namespace CB::feature::camera {

    /// @brief The camera's world yaw, in Skyrim's convention (angle.z: radians clockwise from +y), while CB drives the
    ///        third-person camera.
    /// @return false while it does not: bCamera off, True Directional Movement or SmoothCam loaded, VR, another camera
    ///         state current, or no camera frame within the last quarter second. Thread-safe.
    bool ControlYaw(float& yaw);

}  // namespace CB::feature::camera
