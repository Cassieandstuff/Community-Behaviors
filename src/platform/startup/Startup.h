#pragma once

/// @file
/// @brief Start-up phases a feature can run at: it registers during static initialization, the plugin runs the phase.

namespace CB::platform::startup {

    /// @brief Runs `fn` once at SKSE's kDataLoaded (every plugin file loaded, before the main menu loads a game).
    /// @param who names the caller in the log
    /// @pre   Called before the phase runs (a feature's static initializer).
    /// @return false (and logs) once the phase has run: `fn` will never run.
    bool OnDataLoaded(void (*fn)(), const char* who);

    /// @brief Runs every OnDataLoaded callback, in registration order.
    /// @pre   Called once, from the plugin's kDataLoaded handler.
    void RunDataLoaded();

}  // namespace CB::platform::startup
