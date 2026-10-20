#pragma once

// The cold-compile progress bar, drawn PASSIVELY over the game's own frames: a call detour (CallSite) on the
// engine's internal present CALL (OAR's hook point). We never call Present ourselves — the earlier
// forced-present pump fought the game for the swapchain (black menu on native D3D11, an uncatchable async
// crash on DXVK). Instead this rides the GAME's present: the detour runs the original call first, then
// renders the ImGui bar ON TOP of the finished frame. ImGui (own context, DX11 + Win32 backends) is built
// eagerly at Install() from the swapchain's own device + window.
//
// For the bar to be visible the game must be presenting WHILE the compile runs, which only happens if
// the heavy behavior compile is deferred to a background thread (see the split in CompileGate): the
// menu comes up and presents, our hook draws the bar, and byteserve's backstop waits on the compile
// only when a graph is actually needed. The bar reads ProgressOverlay; when the compile finishes
// (running=false) the hook draws nothing.
namespace CB::core::bootstrap {

    /// @brief Builds ImGui from the swapchain so the present-call hook (attached at plugin load) starts drawing.
    /// @return false while the swapchain isn't up yet (retry later); true once installed (idempotent).
    bool Install();

    /// @brief Something else drawn with the same ImGui context over the finished frame (a debug HUD).
    /// @note `active` runs on the render thread every present; `draw` only when it returns true, between
    ///       ImGui::NewFrame and ImGui::Render, so it may use any ImGui call (windows, draw lists).
    struct Overlay {
        const char* who    = "?";
        bool (*active)()   = nullptr;
        void (*draw)()     = nullptr;
    };

    /// @brief Adds an overlay. Call during static initialization or SKSEPluginLoad only: the list is read without a
    ///        lock once the present hook draws.
    void RegisterOverlay(const Overlay& overlay);

}  // namespace CB::core::bootstrap
