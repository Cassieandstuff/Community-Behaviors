#include "PCH.h"

#include "core/bootstrap/ProgressHud.h"
#include "core/bootstrap/ProgressOverlay.h"

#include <atomic>
#include <cstdio>
#include <vector>

#include <d3d11.h>
#include <dxgi.h>

#include <imgui.h>
#include <backends/imgui_impl_dx11.h>
#include <backends/imgui_impl_win32.h>

#include <platform/game/Render.h>

// Startup progress bar, modelled on Community Shaders' Menu (src/Menu.cpp Init + src/Hooks.cpp draw):
//   • ImGui context + backends + font/device objects are built ONCE, EAGERLY, at Install() time —
//     off the live present path — exactly like CS's Menu::Init at renderer init. The OLD design built
//     all of that lazily inside the FIRST hooked Present (a heavy render-thread hitch — font-atlas GPU
//     upload — landing mid-frame on a live menu, which stalled presentation and ghosted/minimized the
//     borderless window). Building it up front makes the first real bar frame cheap.
//   • Each frame draws to the render target the engine already has bound after its present call (see
//     DrawBar) — never one of our own, which drew off-screen under Community Shaders.
//   • Install() is called EARLY (kDataLoaded, before the menu), not at the late compile gate, so the bar
//     is live from the first frame and the eager init is nowhere near the live-present critical path.
namespace CB::core::bootstrap {

    namespace {
        // We hook the GAME'S internal present CALL (OAR's hook point, platform/game/Render.h), not the raw
        // IDXGISwapChain::Present vtable. The vtable hook drew a valid frame (correct size, real geometry, valid
        // draw data — proven by the DIAG log) that never appeared: Community Shaders owns the render pipeline and
        // composites over a raw-vtable draw. Hooking the game's present call — original first, then our draw —
        // lands the bar ON TOP of everything the engine + CS rendered, the way OAR's overlay coexists with CS.
        ID3D11Device*        s_device      = nullptr;
        ID3D11DeviceContext* s_ctx         = nullptr;
        std::atomic<bool>    s_imguiReady{ false };
        std::atomic<bool>    s_installed{ false };

        // EAGER ImGui init — context + platform/renderer backends + device objects (font atlas), built
        // ON THE CALLING THREAD at Install time, before the present-call hook draws anything. Everything the first
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

        std::vector<Overlay>& Overlays()
        {
            static std::vector<Overlay> overlays;
            return overlays;
        }

        // Draw the bar ON TOP of the frame the game just rendered (no clear — the game's frame stays), to the
        // already-bound target. Only while the compile is running.
        void DrawBar(std::size_t done, std::size_t total)
        {

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
            if (total == 0) {
                // Indeterminate: the gate armed the bar before Init finished, so the real total isn't
                // known yet. A negative fraction makes ImGui animate a sweeping bar ("working…").
                ImGui::ProgressBar(-1.0f * static_cast<float>(ImGui::GetTime()),
                                   ImVec2(460.0f, 24.0f), "compiling\xE2\x80\xA6");   // "compiling…"
            } else {
                const float frac = static_cast<float>(done) / static_cast<float>(total);
                char label[64];
                std::snprintf(label, sizeof label, "%zu / %zu", done, total);
                ImGui::ProgressBar(frac, ImVec2(460.0f, 24.0f), label);
            }
            ImGui::End();

            ImGui::PopStyleVar(5);
            ImGui::PopStyleColor(5);
        }

        /// @brief One ImGui frame per present, built only when the bar or an overlay has something to draw.
        void DrawFrame()
        {
            std::size_t done = 0, total = 0;
            const bool  bar = CB::core::bootstrap::ReadProgress(done, total);
            bool        any = bar;
            for (const Overlay& o : Overlays()) any = any || (o.active && o.active());
            if (!any) return;

            ImGui_ImplDX11_NewFrame();
            ImGui_ImplWin32_NewFrame();
            ImGui::NewFrame();
            if (bar) DrawBar(done, total);
            for (const Overlay& o : Overlays())
                if (o.active && o.draw && o.active()) o.draw();
            ImGui::Render();

            // Draw to the currently-bound target — do NOT bind our own. At this hook point (right after the
            // game's present call) the engine's framebuffer RTV is already bound, so RenderDrawData lands the
            // bar on the exact surface being scanned out. Binding our own renderView/buffer-0 RTV instead put
            // valid geometry onto a surface Community Shaders wasn't presenting — the draw ran but never
            // showed. This is OAR's method, which is why it coexists with CS.
            ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        }

        // Our detour on the game's present call. Original FIRST (the engine + CS render the finished
        // frame), THEN our bar on top — OAR's ordering, which is why its overlay survives CS.
        void OnPresent(platform::game::PresentCall::Next next, std::uint32_t a_arg)
        {
            next(a_arg);
            if (s_imguiReady.load(std::memory_order_acquire))
                DrawFrame();
        }

        /// @note Subscribed at static initialization, so the call detour attaches with every other hook at plugin
        ///       load; it draws nothing until Install() has built ImGui (kDataLoaded, once the swapchain exists).
        [[maybe_unused]] const bool s_presentSubscribed =
            platform::game::PresentCall::Subscribe(&OnPresent, platform::hooks::Priority::Normal, "ProgressHud");
    }  // namespace

    void RegisterOverlay(const Overlay& overlay)
    {
        Overlays().push_back(overlay);
    }

    bool Install()
    {
        if (s_installed.load(std::memory_order_acquire)) return true;

        // The present call itself is hooked at plugin load (OnPresent's subscription, attached by InstallAll); here
        // ImGui only becomes ready for it to draw. Without the hook there is nothing to draw into: build nothing.
        if (!platform::game::PresentCall::Installed()) {
            LOG_ERROR("ProgressHud: the present call is not hooked (no pin on this runtime, or not attached) — no "
                      "compile progress bar.");
            return false;
        }

        auto* win  = RE::BSGraphics::Renderer::GetCurrentRenderWindow();
        auto* swap = win ? reinterpret_cast<IDXGISwapChain*>(win->swapChain) : nullptr;
        if (!swap) {
            LOG_INFO("ProgressHud: swapchain not up yet — install deferred (will retry at the compile gate).");
            return false;
        }

        // Build imgui + its device objects up front, before the hook draws anything, so the first live present
        // that draws the bar does zero heavy work (the CS Menu::Init pattern).
        if (!InitImGui(swap)) {
            LOG_ERROR("ProgressHud: eager imgui init failed — no compile progress bar.");
            return false;
        }

        s_installed.store(true, std::memory_order_release);
        LOG_INFO("ProgressHud: ready — the bar draws over the finished frame (OAR's present-call hook point).");
        return true;
    }

}  // namespace CB::core::bootstrap
