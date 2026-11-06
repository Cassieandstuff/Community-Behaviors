#include "PCH.h"

#include "core/ui/UiHost.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <vector>

#include <d3d11.h>
#include <dxgi.h>

#include <imgui.h>
#include <backends/imgui_impl_dx11.h>
#include <backends/imgui_impl_win32.h>

#include <platform/game/Render.h>

/// @file
/// @brief Modelled on Community Shaders' Menu (src/Menu.cpp Init + src/Hooks.cpp draw): the context, the backends and the
///        font and device objects are built once and eagerly in Install(), off the live present path, so the first frame
///        that draws does no heavy work. The earlier design built them inside the first hooked Present, a render-thread
///        hitch (the font-atlas upload) that stalled presentation and ghosted the borderless window.

namespace CB::core::ui {

    namespace {

        ID3D11Device*        s_device = nullptr;
        ID3D11DeviceContext* s_ctx    = nullptr;
        std::atomic<bool>    s_imguiReady{ false };
        std::atomic<bool>    s_installed{ false };

        /// @brief Context, platform and renderer backends and device objects, on the calling thread.
        /// @return false when the swapchain or its device is not ready (the caller retries later).
        /// @note   One complete no-op frame (NewFrame, EndFrame; no Render, no target bind) makes the DX11 backend create
        ///         its shaders, buffers and font atlas texture now. DisplaySize is set explicitly in case the window's
        ///         client rect is not queryable this early (NewFrame would assert).
        bool InitImGui(IDXGISwapChain* swap)
        {
            DXGI_SWAP_CHAIN_DESC desc{};
            if (FAILED(swap->GetDesc(&desc))) return false;
            if (FAILED(swap->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&s_device))) || !s_device)
                return false;
            s_device->GetImmediateContext(&s_ctx);
            if (!s_ctx) return false;

            IMGUI_CHECKVERSION();
            ImGui::CreateContext();
            ImGuiIO& io    = ImGui::GetIO();
            io.IniFilename = nullptr;
            io.LogFilename = nullptr;
            io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;   // non-interactive; don't touch the cursor
            ImGui::StyleColorsDark();
            ImGui_ImplWin32_Init(desc.OutputWindow);
            ImGui_ImplDX11_Init(s_device, s_ctx);

            ImGui_ImplDX11_NewFrame();
            ImGui_ImplWin32_NewFrame();
            io.DisplaySize = ImVec2(static_cast<float>(desc.BufferDesc.Width),
                                    static_cast<float>(desc.BufferDesc.Height));
            ImGui::NewFrame();
            ImGui::EndFrame();

            s_imguiReady.store(true, std::memory_order_release);
            LOG_INFO("ui: imgui pre-initialised early ({}x{}) - font/device objects built off the present path "
                     "(Community Shaders pattern).", desc.BufferDesc.Width, desc.BufferDesc.Height);
            return true;
        }

        std::vector<Overlay>& Overlays()
        {
            static std::vector<Overlay> overlays;
            return overlays;
        }

        /// @brief One ImGui frame per present, built only when some overlay has something to draw.
        /// @note  Draws to the render target the engine already has bound right after its present call, and binds none
        ///        of its own: binding the swapchain's buffer-0 RTV drew valid geometry onto a surface Community Shaders
        ///        was not presenting, so it never showed. This is the present-call method Open Animation Replacer's
        ///        overlay uses, which is why it coexists with Community Shaders.
        void DrawFrame()
        {
            bool any = false;
            for (const Overlay& o : Overlays()) any = any || (o.active && o.active());
            if (!any) return;

            ImGui_ImplDX11_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();
            for (const Overlay& o : Overlays())
                if (o.active && o.draw && o.active()) o.draw();
            ImGui::Render();
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        }

        /// @brief Our detour on the game's present call: the original first (the engine and Community Shaders render the
        ///        finished frame), then our frame on top.
        void OnPresent(platform::game::PresentCall::Next next, std::uint32_t a_arg)
        {
            next(a_arg);
            if (s_imguiReady.load(std::memory_order_acquire))
                DrawFrame();
        }

        /// @note Subscribed at static initialization, so the call detour attaches with every other hook at plugin load; it
        ///       draws nothing until Install() has built ImGui (kDataLoaded, once the swapchain exists). The hook is
        ///       the game's internal present CALL, not the raw IDXGISwapChain::Present vtable: a vtable hook drew a valid
        ///       frame that never appeared, because Community Shaders composites over a raw-vtable draw.
        [[maybe_unused]] const bool s_presentSubscribed =
            platform::game::PresentCall::Subscribe(&OnPresent, platform::hooks::Priority::Normal, "UiHost");

    }  // namespace

    void RegisterOverlay(const Overlay& overlay)
    {
        auto& all = Overlays();
        all.push_back(overlay);
        std::ranges::stable_sort(all, {}, &Overlay::order);
    }

    bool Install()
    {
        if (s_installed.load(std::memory_order_acquire)) return true;

        if (!platform::game::PresentCall::Installed()) {
            LOG_ERROR("ui: the present call is not hooked (no pin on this runtime, or not attached) - nothing can be drawn.");
            return false;
        }

        auto* win  = RE::BSGraphics::Renderer::GetCurrentRenderWindow();
        auto* swap = win ? reinterpret_cast<IDXGISwapChain*>(win->swapChain) : nullptr;
        if (!swap) {
            LOG_INFO("ui: swapchain not up yet - install deferred (retried at the compile gate).");
            return false;
        }

        if (!InitImGui(swap)) {
            LOG_ERROR("ui: eager imgui init failed - nothing can be drawn.");
            return false;
        }

        s_installed.store(true, std::memory_order_release);
        LOG_INFO("ui: ready - overlays draw over the finished frame (the present-call hook point).");
        return true;
    }

}  // namespace CB::core::ui
