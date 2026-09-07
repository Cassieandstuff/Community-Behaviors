#pragma once

// The cold-compile progress bar, drawn the way every coexisting overlay mod (CommunityShaders, OAR,
// SKSE Menu Framework) draws: a PASSIVE hook on IDXGISwapChain::Present (vtable index 8). We never
// call Present ourselves — the earlier forced-present pump fought the game for the swapchain (black
// menu on native D3D11, an uncatchable async crash on DXVK). Instead this rides the GAME's present:
// each time the engine presents a frame, our hook renders the ImGui bar ON TOP of that frame and then
// calls the original Present. ImGui (own context, DX11 + Win32 backends) is initialised lazily on the
// first hooked present, from the swapchain's own device + window — exactly the OAR/CS pattern.
//
// For the bar to be visible the game must be presenting WHILE the compile runs, which only happens if
// the heavy behavior compile is deferred to a background thread (see the split in CompileGate): the
// menu comes up and presents, our hook draws the bar, and byteserve's backstop waits on the compile
// only when a graph is actually needed. The bar reads ProgressOverlay; when the compile finishes
// (running=false) the hook renders nothing and just forwards Present.
namespace CB::ProgressHud {

    // Install the Present vtable hook (once). Safe to call before the swapchain exists — returns false
    // then and can be retried. No-op if already installed.
    bool Install();

}  // namespace CB::ProgressHud
