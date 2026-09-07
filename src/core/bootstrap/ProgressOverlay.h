#pragma once

#include <cstddef>

// Community Behaviors's cold-compile progress STATE — three atomics written by the background
// compile (WarmUpThread) and read by the ProgressHud present hook that draws the bar.
//
// This used to draw through SKSE Menu Framework. It now hands off to ProgressHud, which rides the
// game's own IDXGISwapChain::Present (the way CommunityShaders/OAR do) and draws the bar while the
// SPLIT-path background compile runs and the menu presents. This header is just the thread-safe
// hand-off of progress from the compile thread to that renderer.
namespace CB::ProgressOverlay {

    // Publish progress (compile thread). running=false marks the compile finished.
    void SetProgress(std::size_t done, std::size_t total, bool running);

    // Read the latest progress (render/pump thread). Returns whether the compile is still running.
    bool ReadProgress(std::size_t& done, std::size_t& total);

}  // namespace CB::ProgressOverlay
