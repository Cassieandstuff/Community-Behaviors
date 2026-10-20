#pragma once

#include <PCH.h>

#include <platform/hooks/CallSite.h>

#include <cstdint>

/// @file
/// @brief Call sites of the render loop.

namespace CB::platform::game {

    /// @brief The render loop's CALL of the present function (OAR's hook point). A subscriber that calls `next` first
    ///        and then draws lands on top of the finished frame, with the engine's framebuffer target still bound.
    /// @note Why this call and not IDXGISwapChain::Present: Community Shaders owns the swapchain and composites over a
    ///       raw vtable hook's draw; drawing after the game's own present call coexists with it, as OAR's overlay does.
    using PresentCall = hooks::CallSite<hooks::engine::Renderer_PresentCall, void(std::uint32_t)>;

}  // namespace CB::platform::game
