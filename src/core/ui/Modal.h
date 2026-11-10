#pragma once

#include <functional>
#include <string>
#include <vector>

/// @file
/// @brief A prompt over the game: a title, a text and a row of buttons, one answer. The first client of the UI host's
///        input: while one is up the game gets no input and ImGui draws a cursor.

namespace CB::core::ui {

    struct ModalDesc {
        std::string              title;
        std::string              text;
        std::vector<std::string> choices;         ///< button labels, left to right; at least one
        int                      defaultChoice = 0;   ///< what Enter, Escape and a lapse answer: make it the safe one
    };

    /// @param choice    the index of the button chosen
    /// @param byDefault true when nobody chose: input was not available (SE, VR), or the game stopped pumping input while
    ///                  the prompt was up. `choice` is then the default.
    using ModalAnswer = std::function<void(int choice, bool byDefault)>;

    /// @brief Shows `desc` after any prompt already up and calls `answer` once.
    /// @param answer runs on the present call's thread, or at once on the calling thread when input cannot be taken.
    /// @note  Callable from any thread.
    void ShowModal(ModalDesc desc, ModalAnswer answer);

}  // namespace CB::core::ui
