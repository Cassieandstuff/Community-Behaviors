#pragma once

#include <cstddef>

// Community Behaviors's cold-compile progress STATE — three atomics written by the background
// compile (WarmUpThread) and read by the ProgressHud present hook that draws the bar.
//
// ProgressHud draws the bar from the game's own present call (OAR's hook point) while the background
// compile runs and the menu presents. This header is just the thread-safe hand-off of progress from
// the compile thread to that renderer.
namespace CB::core::bootstrap {

    // Publish progress (compile thread). running=false marks the compile finished.
    void SetProgress(std::size_t done, std::size_t total, bool running);

    // Read the latest progress (render/pump thread). Returns whether the compile is still running.
    bool ReadProgress(std::size_t& done, std::size_t& total);

}  // namespace CB::core::bootstrap
