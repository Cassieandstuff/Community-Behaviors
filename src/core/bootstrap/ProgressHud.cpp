#include "PCH.h"

#include "core/bootstrap/ProgressHud.h"
#include "core/bootstrap/ProgressOverlay.h"

#include <atomic>
#include <cstdio>

#include <d3d11.h>
#include <dxgi.h>

#include <imgui.h>
#include <backends/imgui_impl_dx11.h>
#include <backends/imgui_impl_win32.h>

#include <MinHook.h>

// Startup progress bar, modelled on Community Shaders' Menu (src/Menu.cpp Init + src/Hooks.cpp draw):
//   • ImGui context + backends + font/device objects are built ONCE, EAGERLY, at Install() time —
//     off the live present path — exactly like CS's Menu::Init at renderer init. The OLD design built
//     all of that lazily inside the FIRST hooked Present (a heavy render-thread hitch — font-atlas GPU
//     upload — landing mid-frame on a live menu, which stalled presentation and ghosted/minimized the
//     borderless window). Building it up front makes the first real bar frame cheap.
//   • Each frame draws to a FRESH render-target view off the CURRENT backbuffer (created + released per
//     present), so a swapchain resize/transition can never leave us pointing at a stale target — the CS
//     "always use the live framebuffer" principle, adapted (CS uses the game's kFRAMEBUFFER RTV; we
//     recreate one from the swapchain, which needs no game-render-target plumbing and is only paid while
//     the compile is running).
//   • Install() is called EARLY (kDataLoaded, before the menu), not at the late compile gate, so the bar
//     is live from the first frame and the eager init is nowhere near the live-present critical path.
namespace CB::ProgressHud {

    namespace {
        using PresentFn = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);

        PresentFn            s_origPresent = nullptr;
        ID3D11Device*        s_device      = nullptr;
        ID3D11DeviceContext* s_ctx         = nullptr;
        std::atomic<bool>    s_imguiReady{ false };
        std::atomic<bool>    s_installed{ false };
        unsigned             s_frames = 0;

        // EAGER ImGui init — context + platform/renderer backends + device objects (font atlas), built
        // ON THE CALLING THREAD at Install time, before the Present hook is enabled. Everything the first
        // live present used to build lazily is done here instead. Returns false if the swapchain/device
        // isn't ready yet (caller retries later). Runs once; single-threaded (no draw can race it because
        // the hook isn't enabled until this returns).
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

            // Force the DX11 backend to build its device objects (shaders, buffers, and the FONT ATLAS
            // texture) NOW — ImGui_ImplDX11_NewFrame creates them when missing. Run one complete, balanced
            // no-op frame (NewFrame → EndFrame, no Render/target bind) so the heavy GPU upload happens here,
            // off the live-present path, instead of inside the first hooked present. DisplaySize is set
            // explicitly in case the window client rect isn't queryable this early (avoids a NewFrame assert).
            ImGui_ImplDX11_NewFrame();
            ImGui_ImplWin32_NewFrame();
            io.DisplaySize = ImVec2(static_cast<float>(desc.BufferDesc.Width),
                                    static_cast<float>(desc.BufferDesc.Height));
            ImGui::NewFrame();
            ImGui::EndFrame();

            s_imguiReady.store(true, std::memory_order_release);
            LOG_INFO("ProgressHud: imgui pre-initialised early ({}x{}) — font/device objects built off the "
                     "present path (Community Shaders pattern).", desc.BufferDesc.Width, desc.BufferDesc.Height);
            return true;
        }

        // Draw the bar ON TOP of the frame the game just rendered (no clear — the game's frame stays), to a
        // FRESH RTV off the current backbuffer. Only while the compile is running.
        void DrawBar(IDXGISwapChain* swap)
        {
            std::size_t done = 0, total = 0;
            if (!ProgressOverlay::ReadProgress(done, total)) return;   // compile not running → nothing to draw

            // Live framebuffer, this frame — recreate the RTV each present so a resize/mode transition can
            // never hand us a stale backbuffer (the class of bug that knocks a swapchain around at load).
            ID3D11Texture2D* back = nullptr;
            if (FAILED(swap->GetBuffer(0, IID_PPV_ARGS(&back))) || !back) return;
            ID3D11RenderTargetView* rtv = nullptr;
            s_device->CreateRenderTargetView(back, nullptr, &rtv);
            back->Release();
            if (!rtv) return;

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
            s_ctx->OMSetRenderTargets(1, &rtv, nullptr);
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
            rtv->Release();

            if (s_frames++ == 0)
                LOG_INFO("ProgressHud: first bar frame drawn inside the game present.");
        }

        HRESULT STDMETHODCALLTYPE HookPresent(IDXGISwapChain* swap, UINT syncInterval, UINT flags)
        {
            if (s_imguiReady.load(std::memory_order_acquire))
                DrawBar(swap);   // imgui already fully initialised at Install — no lazy build here
            return s_origPresent(swap, syncInterval, flags);
        }
    }  // namespace

    bool Install()
    {
        if (s_installed.load(std::memory_order_acquire)) return true;

        auto* win  = RE::BSGraphics::Renderer::GetCurrentRenderWindow();
        auto* swap = win ? reinterpret_cast<IDXGISwapChain*>(win->swapChain) : nullptr;
        if (!swap) {
            LOG_INFO("ProgressHud: swapchain not up yet — install deferred (will retry at the compile gate).");
            return false;
        }

        // Build imgui + its device objects up front, BEFORE enabling the hook, so the first live present
        // that draws the bar does zero heavy work (the CS Menu::Init pattern).
        if (!InitImGui(swap)) {
            LOG_ERROR("ProgressHud: eager imgui init failed — no compile progress bar.");
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
