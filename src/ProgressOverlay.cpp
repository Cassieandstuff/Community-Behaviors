#include "PCH.h"

#include "ProgressOverlay.h"

#include <atomic>
#include <cstdint>
#include <string>

namespace CB::ProgressOverlay {

    namespace {

        // ImGui's ImVec2, passed by value to the cimgui `ig*` exports (matches SMF's ABI).
        struct Vec2 { float x, y; };

        using RegHud_t     = std::int64_t (*)(void(__stdcall*)());
        using Begin_t      = bool (*)(const char*, bool*, int);
        using End_t        = void (*)();
        using TextU_t      = void (*)(const char*, const char*);
        using Progress_t   = void (*)(float, Vec2, const char*);
        using SetPos_t     = void (*)(Vec2, int, Vec2);
        using SetBgAlpha_t = void (*)(float);

        Begin_t      igBegin       = nullptr;
        End_t        igEnd         = nullptr;
        TextU_t      igText        = nullptr;
        Progress_t   igProgressBar = nullptr;
        SetPos_t     igSetPos      = nullptr;
        SetBgAlpha_t igSetBgAlpha  = nullptr;

        std::atomic<std::size_t> s_done{ 0 };
        std::atomic<std::size_t> s_total{ 0 };
        std::atomic<bool>        s_running{ false };

        // Core ImGuiWindowFlags (stable values): NoTitleBar|NoResize|NoMove|NoScrollbar|
        // AlwaysAutoResize|NoSavedSettings|NoFocusOnAppearing.
        constexpr int kFlags      = (1 << 0) | (1 << 1) | (1 << 2) | (1 << 3) |
                                    (1 << 6) | (1 << 8) | (1 << 12);
        constexpr int kCondAlways = 1 << 0;

        // SMF HUD draw callback — invoked on the render thread each frame SMF renders.
        void __stdcall Draw()
        {
            if (!s_running.load() || !igBegin) return;

            const std::size_t done  = s_done.load();
            const std::size_t total = s_total.load();
            const float       frac  = total ? static_cast<float>(done) / static_cast<float>(total) : 0.0f;

            igSetPos(Vec2{ 60.0f, 60.0f }, kCondAlways, Vec2{ 0.0f, 0.0f });
            igSetBgAlpha(0.85f);
            if (igBegin("##CommunityBehaviorsPrecompile", nullptr, kFlags)) {
                igText("Community Behaviors  -  compiling behaviors", nullptr);
                const std::string label = std::to_string(done) + " / " + std::to_string(total);
                igProgressBar(frac, Vec2{ 360.0f, 0.0f }, label.c_str());
            }
            igEnd();
        }

    }  // namespace

    void SetProgress(std::size_t done, std::size_t total, bool running)
    {
        s_done    = done;
        s_total   = total;
        s_running = running;
    }

    void Install()
    {
        const HMODULE smf = GetModuleHandleW(L"SKSEMenuFramework");
        if (!smf) {
            LOG_INFO("Community Behaviors: SKSE Menu Framework not loaded — precompile progress via log only.");
            return;
        }

        igBegin       = reinterpret_cast<Begin_t>(GetProcAddress(smf, "igBegin"));
        igEnd         = reinterpret_cast<End_t>(GetProcAddress(smf, "igEnd"));
        igText        = reinterpret_cast<TextU_t>(GetProcAddress(smf, "igTextUnformatted"));
        igProgressBar = reinterpret_cast<Progress_t>(GetProcAddress(smf, "igProgressBar"));
        igSetPos      = reinterpret_cast<SetPos_t>(GetProcAddress(smf, "igSetNextWindowPos"));
        igSetBgAlpha  = reinterpret_cast<SetBgAlpha_t>(GetProcAddress(smf, "igSetNextWindowBgAlpha"));
        auto reg      = reinterpret_cast<RegHud_t>(GetProcAddress(smf, "RegisterHudElement"));

        if (!reg || !igBegin || !igEnd || !igText || !igProgressBar || !igSetPos || !igSetBgAlpha) {
            LOG_ERROR("Community Behaviors: SKSE Menu Framework is missing expected exports — no progress overlay.");
            igBegin = nullptr;  // keep Draw inert
            return;
        }

        reg(&Draw);
        LOG_INFO("Community Behaviors: precompile progress overlay registered with SKSE Menu Framework.");
    }

}  // namespace CB::ProgressOverlay
