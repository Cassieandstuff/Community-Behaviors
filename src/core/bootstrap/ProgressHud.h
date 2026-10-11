#pragma once

// The cold-compile progress bar, drawn PASSIVELY over the game's own frames: a trampoline detour on the
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

    /// @brief Install the present-call detour (once).
    /// @return false while the swapchain isn't up yet (retry later); true once installed (idempotent).
    bool Install();

}  // namespace CB::core::bootstrap
