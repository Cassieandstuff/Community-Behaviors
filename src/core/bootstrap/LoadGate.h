#pragma once

namespace CB::core::bootstrap {

    /// @brief Holds every game load and every New Game until the warm-up has finished (CB::WarmUpFinished: graphs,
    ///        native animations, adsf/setdata and the features' warm-up), without parking a thread on it.
    /// @note  Loads: the save/load manager's main-thread request pump is skipped while the warm-up runs, so the load
    ///        request the save read posts back stays queued and runs on the first frame after. New Game: the menu's
    ///        click is deferred and replayed on the main thread (SKSE task) once the warm-up has finished.
    /// @note  Before this, nothing held the menu: only the opens of graphs CB owns waited (ByteServe), so a game
    ///        loaded before the features' warm-up (motion matching's databases) or with graphs CB doesn't gate.
    /// @pre   SKSEPluginLoad, before hooks::InstallAll.
    void InstallLoadGate();

}  // namespace CB::core::bootstrap
