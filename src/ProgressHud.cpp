#include "PCH.h"

#include "ProgressHud.h"
#include "ProgressOverlay.h"

#include <atomic>
#include <cstdio>

#include <d3d11.h>
#include <dxgi.h>

#include <imgui.h>
#include <backends/imgui_impl_dx11.h>
#include <backends/imgui_impl_win32.h>

#include <MinHook.h>

namespace CB::ProgressHud {

    namespace {
        using PresentFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);

        PresentFn               s_origPresent = nullptr;
        ID3D11Device*           s_device      = nullptr;
        ID3D11DeviceContext*    s_ctx         = nullptr;
        ID3D11RenderTargetView* s_rtv         = nullptr;
        std::atomic<bool>       s_imguiReady{ false };
        std::atomic<bool>       s_installed{ false };
        unsigned                s_frames = 0;

        // Lazy ImGui init on the first hooked present — device/context/window come from the swapchain
        // the game just presented, so everything is guaranteed live. (OAR/CS do exactly this.)
        void EnsureImGui(IDXGISwapChain* swap)
        {
            if (s_imguiReady.load(std::memory_order_acquire)) return;

            DXGI_SWAP_CHAIN_DESC desc{};
            if (FAILED(swap->GetDesc(&desc))) return;
            if (FAILED(swap->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&s_device))) || !s_device)
                return;
            s_device->GetImmediateContext(&s_ctx);

            ID3D11Texture2D* back = nullptr;
            if (SUCCEEDED(swap->GetBuffer(0, IID_PPV_ARGS(&back))) && back) {
                s_device->CreateRenderTargetView(back, nullptr, &s_rtv);
                back->Release();
            }
            if (!s_ctx || !s_rtv) return;

            IMGUI_CHECKVERSION();
            ImGui::CreateContext();
            ImGuiIO& io    = ImGui::GetIO();
            io.IniFilename = nullptr;
            io.LogFilename = nullptr;
            io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;   // non-interactive; don't touch the cursor
            ImGui::StyleColorsDark();
            ImGui_ImplWin32_Init(desc.OutputWindow);
            ImGui_ImplDX11_Init(s_device, s_ctx);

            s_imguiReady.store(true, std::memory_order_release);
            LOG_INFO("ProgressHud: imgui initialised on the game swapchain ({}x{}).",
                     desc.BufferDesc.Width, desc.BufferDesc.Height);
        }

        // Draw the bar ON TOP of the frame the game just rendered (no clear — the game's frame stays).
        void DrawBar()
        {
            std::size_t done = 0, total = 0;
            if (!ProgressOverlay::ReadProgress(done, total)) return;   // compile not running → nothing to draw

            ImGui_ImplDX11_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();

            // Palette: yellow fill inside a white-outlined track, in a clear panel with a white border.
            const ImVec4 kWhite { 1.00f, 1.00f, 1.00f, 1.00f };
            const ImVec4 kYellow{ 1.00f, 0.82f, 0.00f, 1.00f };
            const ImVec4 kClear { 0.00f, 0.00f, 0.00f, 0.00f };   // fully transparent panel
            const ImVec4 kTrack { 0.00f, 0.00f, 0.00f, 0.30f };   // faint dark inside the white outline

            ImGui::PushStyleColor(ImGuiCol_WindowBg,      kClear);
            ImGui::PushStyleColor(ImGuiCol_Border,        kWhite);   // window + frame outline
            ImGui::PushStyleColor(ImGuiCol_Text,          kWhite);
            ImGui::PushStyleColor(ImGuiCol_FrameBg,       kTrack);   // unfilled part of the bar
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, kYellow);  // the fill
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 2.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize,  2.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,   0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,    0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,    ImVec2(18.0f, 16.0f));

            const ImGuiIO& io = ImGui::GetIO();
            ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.5f, io.DisplaySize.y * 0.80f),
                                    ImGuiCond_Always, ImVec2(0.5f, 0.5f));
            ImGui::Begin("##CommunityBehaviorsPrecompile", nullptr,
                         ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_AlwaysAutoResize |
                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoInputs |
                         ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing);
            ImGui::TextUnformatted("Community Behaviors  -  compiling behaviors");
            ImGui::Spacing();
            const float frac = total ? static_cast<float>(done) / static_cast<float>(total) : 0.0f;
            char label[64];
            std::snprintf(label, sizeof label, "%zu / %zu", done, total);
            ImGui::ProgressBar(frac, ImVec2(460.0f, 24.0f), label);
            ImGui::End();

            ImGui::PopStyleVar(5);
            ImGui::PopStyleColor(5);

            ImGui::Render();
            s_ctx->OMSetRenderTargets(1, &s_rtv, nullptr);
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

            if (s_frames++ == 0)
                LOG_INFO("ProgressHud: first bar frame drawn inside the game present.");
        }

        HRESULT STDMETHODCALLTYPE HookPresent(IDXGISwapChain* swap, UINT syncInterval, UINT flags)
        {
            EnsureImGui(swap);
            if (s_imguiReady.load(std::memory_order_acquire))
                DrawBar();
            return s_origPresent(swap, syncInterval, flags);
        }
    }  // namespace

    bool Install()
    {
        if (s_installed.load(std::memory_order_acquire)) return true;

        auto* win  = RE::BSGraphics::Renderer::GetCurrentRenderWindow();
        auto* swap = win ? reinterpret_cast<IDXGISwapChain*>(win->swapChain) : nullptr;
        if (!swap) {
            LOG_INFO("ProgressHud: swapchain not up yet — present hook not installed (will retry).");
            return false;
        }

        void** vtbl    = *reinterpret_cast<void***>(swap);
        void*  present = vtbl[8];   // IDXGISwapChain::Present — index 8 (IUnknown 0-2, IDXGIObject 3-6, IDXGIDeviceSubObject 7, Present 8)
        if (MH_CreateHook(present, reinterpret_cast<void*>(&HookPresent),
                          reinterpret_cast<void**>(&s_origPresent)) != MH_OK ||
            MH_EnableHook(present) != MH_OK) {
            LOG_ERROR("ProgressHud: failed to hook swapchain Present — no compile progress bar.");
            return false;
        }
        s_installed.store(true, std::memory_order_release);
        LOG_INFO("ProgressHud: present hook installed (swapchain vtable[8]) — bar rides the game present.");
        return true;
    }

}  // namespace CB::ProgressHud
