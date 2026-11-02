#pragma once

#include <cstdint>

/// @file
/// @brief Advanced Input (bAdvancedInput): CB reads every input event at the game's one dispatch point, before any
///        sink (PlayerControls, menus, the camera) sees it, into its own buffer. This is the query side.
/// @note  Step 1 of CB owning the input layer: the buffer only, every event still reaches the game unchanged. Actions,
///        contexts and capabilities come as the controller needs them, as code here before any config or blueprint.
/// @note  Thread-safe: the buffer is written on the main thread (PollInputDevices) and read from anywhere (the
///        movement frame runs on job threads). Times are real seconds (steady clock), not game time: a hold or a tap
///        is the player's, unaffected by slow time.

namespace CB::feature::input {

    /// @brief The device an event came from; values match RE::INPUT_DEVICE.
    enum class Device : std::uint8_t { Keyboard = 0, Mouse = 1, Gamepad = 2, VirtualKeyboard = 3 };

    /// @brief One physical button: its device and the device's own id code (DirectInput scan code, mouse button,
    ///        XInput button mask), as RE::ButtonEvent::GetIDCode gives it.
    struct Key {
        Device        device = Device::Keyboard;
        std::uint32_t code   = 0;
    };

    /// @brief A stick or the mouse over one frame.
    struct Axis2 {
        float x = 0.0f, y = 0.0f;
    };

    /// @brief The dispatch count: one per PollInputDevices, i.e. per game frame. 0 before the first.
    std::uint64_t Frame();

    /// @brief Whether `key` is held now.
    bool IsDown(Key key);

    /// @brief Whether `key` went down within the last `frames` dispatches (1 = this frame).
    bool Pressed(Key key, std::uint32_t frames = 1);

    /// @brief Whether `key` came up within the last `frames` dispatches (1 = this frame).
    bool Released(Key key, std::uint32_t frames = 1);

    /// @brief Seconds `key` has been held; 0 while up.
    float HeldFor(Key key);

    /// @brief Whether `key` came up this frame after being held at most `maxHold` seconds.
    bool Tapped(Key key, float maxHold = 0.2f);

    /// @brief The left (`right` false) or right stick, `framesAgo` dispatches back (0 = now), up to kHistory - 1.
    Axis2 Stick(bool right, std::uint32_t framesAgo = 0);

    /// @brief The mouse movement summed over one dispatch, `framesAgo` back (0 = now), up to kHistory - 1.
    Axis2 Mouse(std::uint32_t framesAgo = 0);

    /// @brief How many frames of stick and mouse history the buffer keeps.
    inline constexpr std::uint32_t kHistory = 32;

}  // namespace CB::feature::input
