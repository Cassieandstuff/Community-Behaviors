#pragma once

/// @file
/// @brief CB's camera zoom (bCamera): CB owns the boom's length. It reads the wheel as vanilla's zoom offset moved since
///        the frame before and pins that offset still (so vanilla never eases or switches to first person on it), keeps
///        its own distance in game units, and lays the camera rules' zoom goals on it (Camera.cpp).
/// @note  The built-in layer under every rule is the player's preferred zoom: where the camera sits with no zoom rule
///        holding, and where it returns when one lets go. A wheel move lasts until the zoom context changes (a zoom rule
///        starts or ends).

namespace CB::feature::camera::zoom {

    /// @brief The player's zoom settings: settings.ini [Camera], edited live from the menu.
    struct Settings {
        float preferred   = 330.0f;   ///< fPreferredDistance: the boom's length with no zoom rule holding, game units
        bool  manual      = true;     ///< bManualZoom: the wheel moves the camera; off, it always sits at the preferred distance
        bool  firstPerson = true;     ///< bFirstPerson: zooming all the way in and the POV key switch to first person
        float halflife    = 0.12f;    ///< fZoomHalflife: seconds for the boom to close half its gap to the zoom's goal
    };

    /// @brief The settings now. Thread-safe; read from settings.ini once.
    Settings Current();

    /// @brief Use `s` from the next frame on, without saving it.
    void Set(const Settings& s);

    /// @brief Writes the settings to settings.ini [Camera]; false when the file could not be written.
    bool Save();

    /// @brief The boom's length CB's zoom showed last frame (game units, the pitch zoom left out); 0 before any frame.
    float Shown();

    /// @brief The frame's half of Shown.
    void PublishShown(float distance);

}  // namespace CB::feature::camera::zoom
