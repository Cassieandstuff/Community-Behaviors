#pragma once

#include <cstdint>

struct ImGuiIO;

/// @file
/// @brief The UI host's input: while an overlay wants input, the game's input events go to the host instead of the game.
/// @note  Taken at the one place every InputEvent sink (PlayerControls, menus, the camera) is fed from: the dispatch call
///        in BSInputDeviceManager::PollInputDevices, first in its chain so nothing of CB's sees a swallowed event either.
///        AE only (the dispatch has no pin on SE or VR), so on those runtimes nothing can take input.
/// @note  Events are queued on the main thread, where PollInputDevices runs, and handed to ImGui by the host just before
///        its frame, on the present call's thread: ImGui's event queue is not thread-safe.

namespace CB::core::ui {

    /// @return Whether the dispatch is hooked: whether an overlay's wish for input can be granted at all.
    bool CanTakeInput();

    /// @return How many times the game has dispatched its input events (one per game frame); 0 until the first.
    /// @note   A modal that sees this stand still knows the game is not pumping input and nobody can answer it.
    std::uint64_t InputPumps();

    /// @brief Moves the queued events into `io`. Present-call thread only.
    void DrainInput(ImGuiIO& io);

}  // namespace CB::core::ui
