#pragma once

/// @file
/// @brief CB's sprint rules for the player under the movement model (CharMovementModel): the sprint key stays vanilla's
///        toggle, CB decides when the toggle may sprint. Some reasons SUSPEND it (it resumes by itself when they end),
///        others CANCEL it (the key must be pressed again).

namespace CB::feature::controller::sprint {

    struct State {
        bool        wanted    = false;   ///< the player's toggle is on (including while CB holds it suspended)
        bool        suspended = false;   ///< CB cleared the toggle for a suspend reason and will restore it
        bool        sprinting = false;   ///< what the rules asked the engine for this frame
        /// @brief The current suspend or cancel reason, a static string: "lock beyond 50 deg", "blocking", "airborne",
        ///        "stopped", "stamina", "encumbered", "attacking", or "".
        const char* why = "";
    };
    /// @brief False while the rules are off (char_movement_model, or the model not running) or no frame evaluated
    ///        within the last half second. Thread-safe.
    bool Current(State& out);

}  // namespace CB::feature::controller::sprint
