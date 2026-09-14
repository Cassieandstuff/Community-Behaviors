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

#include "Hooks/factory/Install.h"   // hooks::InstallCallDetour — trampoline write_call<5>

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
        // We hook the GAME'S internal present CALL (OAR's hook point), not the raw IDXGISwapChain::Present
        // vtable. The vtable hook drew a valid frame (correct size, real geometry, valid draw data — proven
        // by the DIAG log) that never appeared: Community Shaders owns the render pipeline and composites
        // over a raw-vtable draw. Hooking the game's present call — original first, then our draw — lands
        // the bar ON TOP of everything the engine + CS rendered, the way OAR's overlay coexists with CS.
        using PresentCallFn = void (*)(std::uint32_t);

        PresentCallFn        s_origPresent = nullptr;   // original present call (from the trampoline)
        ID3D11Device*        s_device      = nullptr;
        ID3D11DeviceContext* s_ctx         = nullptr;
        std::atomic<bool>    s_imguiReady{ false };
        std::atomic<bool>    s_installed{ false };

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
        void DrawBar()
        {
            std::size_t done = 0, total = 0;
            if (!ProgressOverlay::ReadProgress(done, total)) return;   // no compile running → nothing to draw

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
        void PresentThunk(std::uint32_t a_arg)
        {
            s_origPresent(a_arg);
            if (s_imguiReady.load(std::memory_order_acquire))
                DrawBar();
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

        // Hook the game's present CALL (OAR's hook point), via the trampoline write_call<5>. This is a
        // CALL to the present function inside the render loop; our detour runs the original then draws the
        // bar on top of the finished frame — coexisting with Community Shaders (the raw DXGI-vtable hook did
        // not: CS composited over it). Requires SKSE::AllocTrampoline (done at plugin load).
        // OAR's present-call site, per runtime: SE id 75461 / AE id 77246 / VR offset 0xDBBDD0, with the
        // CALL sitting at +0x9 (SE/AE) or +0x15 (VR) into that function. version()[1]: 5=SE, 6=AE, 4=VR.
        const auto           ver  = REL::Module::get().version();
        const bool           vr   = ver[1] < 5;
        const std::uintptr_t base = vr ? REL::Relocation<std::uintptr_t>{ REL::Offset(0xDBBDD0) }.address()
                                       : REL::Relocation<std::uintptr_t>{ REL::ID(ver[1] >= 6 ? 77246u : 75461u) }.address();
        const std::uintptr_t site = base + (vr ? 0x15u : 0x9u);
        const std::uintptr_t                  orig = hooks::InstallCallDetour<5>(site, &PresentThunk,
                                                                                 "ProgressHud present");
        if (!orig) {
            LOG_ERROR("ProgressHud: present-call detour not installed — no compile progress bar.");
            return false;
        }
        s_origPresent = reinterpret_cast<PresentCallFn>(orig);
        s_installed.store(true, std::memory_order_release);
        LOG_INFO("ProgressHud: present-call hook installed (OAR hook point) — bar draws over the finished frame.");
        return true;
    }

}  // namespace CB::ProgressHud
