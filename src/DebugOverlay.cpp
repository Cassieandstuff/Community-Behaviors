#include "PCH.h"

#include "DebugOverlay.h"
#include "Watermark.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>

namespace CB::DebugOverlay {

    namespace {

        // ImGui's ImVec2, passed by value to the cimgui `ig*` exports (matches SMF's ABI).
        struct Vec2 { float x, y; };

        using RegHud_t     = std::int64_t (*)(void(__stdcall*)());
        using AddSect_t    = void (*)(const char*, void(__stdcall*)());
        using Begin_t      = bool (*)(const char*, bool*, int);
        using End_t        = void (*)();
        using TextU_t      = void (*)(const char*, const char*);
        using Checkbox_t   = bool (*)(const char*, bool*);
        using SetPos_t     = void (*)(Vec2, int, Vec2);
        using SetBgAlpha_t = void (*)(float);

        Begin_t      igBegin      = nullptr;
        End_t        igEnd        = nullptr;
        TextU_t      igText       = nullptr;
        Checkbox_t   igCheckbox   = nullptr;
        SetPos_t     igSetPos     = nullptr;
        SetBgAlpha_t igSetBgAlpha = nullptr;

        std::atomic<bool> s_hudEnabled{ false };   // toggled by the SMF menu checkbox

        // Core ImGuiWindowFlags (stable values): NoTitleBar|NoResize|NoMove|NoScrollbar|
        // AlwaysAutoResize|NoSavedSettings|NoFocusOnAppearing. (Same set ProgressOverlay uses.)
        constexpr int kFlags      = (1 << 0) | (1 << 1) | (1 << 2) | (1 << 3) |
                                    (1 << 6) | (1 << 8) | (1 << 12);
        constexpr int kCondAlways = 1 << 0;

        // The player's locomotion graph variables the skate hinges on. SpeedSampled is the
        // blendParameter of MT_ForwardBlend; the others give context (raw speed / direction).
        constexpr std::array<const char*, 4> kVars{
            "SpeedSampled", "Speed", "Direction", "TurnDelta"
        };

        // Read a float behavior-graph variable off the player. Render-thread read of a float —
        // tolerant of a torn/stale value (debug only), guarded on the player pointer.
        bool ReadVar(const char* a_name, float& a_out)
        {
            auto* pc = RE::PlayerCharacter::GetSingleton();
            a_out = 0.0f;
            return pc && pc->GetGraphVariableFloat(a_name, a_out);
        }

        // Read the BR watermark (INT32) off the player's root graph. Returns true iff the variable
        // exists — which is proof BR's compiled graph is loaded (a vanilla graph lacks it).
        bool ReadWatermark(std::int32_t& a_out)
        {
            auto* pc = RE::PlayerCharacter::GetSingleton();
            a_out = 0;
            return pc && pc->GetGraphVariableInt(watermark::kWatermarkVar, a_out);
        }

        // Emit the "BR served" boolean row: whether the watermark is present, and (if so) whether its
        // value matches this build — a mismatch means the on-disk cache is stale (older BR compiled it).
        void EmitWatermarkRow()
        {
            if (!igText) return;
            std::int32_t v = 0;
            char buf[96];
            if (ReadWatermark(v)) {
                if (v == watermark::kWatermarkValue)
                    std::snprintf(buf, sizeof buf, "BR served      = YES  (watermark v%d)", v);
                else
                    std::snprintf(buf, sizeof buf, "BR served      = YES but STALE (v%d, build v%d)",
                                  v, watermark::kWatermarkValue);
            } else {
                std::snprintf(buf, sizeof buf, "BR served      = NO  (vanilla graph — watermark absent)");
            }
            igText(buf, nullptr);
        }

        // Emit "<name> = <value>" (or "<name> = (n/a)") rows via the supplied igText.
        void EmitVarRows()
        {
            if (!igText) return;
            char buf[96];
            for (const char* name : kVars) {
                float v = 0.0f;
                if (ReadVar(name, v))
                    std::snprintf(buf, sizeof buf, "%-14s = %.4f", name, v);
                else
                    std::snprintf(buf, sizeof buf, "%-14s = (n/a)", name);
                igText(buf, nullptr);
            }
        }

        // Always-registered HUD element — inert until the menu checkbox enables it.
        void __stdcall DrawHud()
        {
            if (!s_hudEnabled.load() || !igBegin) return;
            igSetPos(Vec2{ 60.0f, 200.0f }, kCondAlways, Vec2{ 0.0f, 0.0f });
            igSetBgAlpha(0.85f);
            if (igBegin("##CommunityBehaviorsLocomotionDebug", nullptr, kFlags)) {
                igText("Community Behaviors  -  debug", nullptr);
                EmitWatermarkRow();
                EmitVarRows();
            }
            igEnd();
        }

        // SMF menu section (SMF > Community Behaviors > Locomotion Debug) — the toggle, plus a live
        // readout (handy even with the HUD off, though the game is paused while the menu is open).
        void __stdcall DrawMenu()
        {
            if (igCheckbox) {
                bool on = s_hudEnabled.load();
                if (igCheckbox("Show locomotion debug HUD (watch SpeedSampled while walking)", &on))
                    s_hudEnabled.store(on);
            }
            EmitWatermarkRow();   // "BR served" boolean — proof BR's compiled graph is loaded
            EmitVarRows();
        }

    }  // namespace

    void Install()
    {
        const HMODULE smf = GetModuleHandleW(L"SKSEMenuFramework");
        if (!smf) {
            LOG_INFO("Community Behaviors: SKSE Menu Framework not loaded — locomotion debug overlay unavailable.");
            return;
        }

        igBegin      = reinterpret_cast<Begin_t>(GetProcAddress(smf, "igBegin"));
        igEnd        = reinterpret_cast<End_t>(GetProcAddress(smf, "igEnd"));
        igText       = reinterpret_cast<TextU_t>(GetProcAddress(smf, "igTextUnformatted"));
        igCheckbox   = reinterpret_cast<Checkbox_t>(GetProcAddress(smf, "igCheckbox"));
        igSetPos     = reinterpret_cast<SetPos_t>(GetProcAddress(smf, "igSetNextWindowPos"));
        igSetBgAlpha = reinterpret_cast<SetBgAlpha_t>(GetProcAddress(smf, "igSetNextWindowBgAlpha"));
        auto regHud  = reinterpret_cast<RegHud_t>(GetProcAddress(smf, "RegisterHudElement"));
        auto addSect = reinterpret_cast<AddSect_t>(GetProcAddress(smf, "AddSectionItem"));

        if (!regHud || !addSect || !igBegin || !igEnd || !igText || !igCheckbox || !igSetPos || !igSetBgAlpha) {
            LOG_ERROR("Community Behaviors: SKSE Menu Framework missing expected exports — no locomotion debug overlay.");
            igBegin = nullptr;   // keep DrawHud inert
            return;
        }

        regHud(&DrawHud);
        addSect("Community Behaviors/Locomotion Debug", &DrawMenu);
        LOG_INFO("Community Behaviors: locomotion debug overlay registered (SMF > Community Behaviors > Locomotion Debug).");
    }

}  // namespace CB::DebugOverlay
