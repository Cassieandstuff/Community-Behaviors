#include "PCH.h"

#include "CharFacing.h"
#include "CharSpeedProfile.h"

#include <platform/startup/Startup.h>

#include <SKSEMenuFramework.h>

/// @file
/// @brief The in-game config prototype: SKSE Menu Framework > Community Behaviors > Movement Speeds, editing the speed
///        profile (CharSpeedProfile) live and saving it to settings.ini.
/// @warning The only translation unit that includes SKSEMenuFramework.h: the header's module handle is a per-TU static,
///          and every ImGui wrapper and registration call reads this TU's copy.

namespace CB::feature::controller::config {

    namespace {

        namespace loco = CB::engine::locomotion;

        /// @note Changes are live from the next frame; only Save writes the ini.
        bool EditGait(const char* label, loco::DirectionSpeeds& d, const loco::DirectionSpeeds& stated, bool record)
        {
            ImGuiMCP::SeparatorText(label);
            bool changed = false;
            changed |= ImGuiMCP::DragFloat("Forward", &d.forward, 1.0f, 0.0f, 2000.0f, "%.0f");
            changed |= ImGuiMCP::DragFloat("Side", &d.side, 1.0f, 0.0f, 2000.0f, "%.0f");
            changed |= ImGuiMCP::DragFloat("Back", &d.back, 1.0f, 0.0f, 2000.0f, "%.0f");
            if (record) ImGuiMCP::TextDisabled("record: %.0f / %.0f / %.0f", stated.forward, stated.side, stated.back);
            return changed;
        }

        void __stdcall RenderSpeeds()
        {
            ImGuiMCP::TextWrapped("The speeds CB's controller moves the player and managed NPCs at, per movement type (the graph's "
                                  "iState). 0 keeps the movement type record's speed. Changes apply at once; Save writes settings.ini.");

            auto*             player  = RE::PlayerCharacter::GetSingleton();
            const std::string current = player ? speed::CurrentType(player) : std::string();
            FrameSpeed        fs;
            if (!current.empty()) {
                if (CurrentSpeed(fs))
                    ImGuiMCP::Text("Player: %s, asked %.0f, moving %.0f", current.c_str(), fs.full, fs.speed);
                else
                    ImGuiMCP::Text("Player: %s", current.c_str());
            }
            ImGuiMCP::Separator();

            for (auto& t : speed::Types()) {
                const std::string header = t.name + (t.name == current ? "  (player)" : "") + (t.builtIn ? "" : "  (custom)");
                ImGuiMCP::PushID(t.name.c_str());
                if (ImGuiMCP::CollapsingHeader(header.c_str())) {
                    if (!t.record) ImGuiMCP::TextDisabled("No movement type record of this name: the engine never switches to it.");
                    loco::SpeedProfile p       = t.profile.value_or(loco::SpeedProfile{});
                    bool               changed = false;
                    changed |= EditGait(t.name.find("Sprinting") != std::string::npos ? "Walk (unused while sprinting)" : "Walk", p.walk,
                                        t.stated.walk, t.record);
                    changed |= EditGait(t.name.find("Sprinting") != std::string::npos ? "Sprint" : "Run", p.run, t.stated.run, t.record);
                    if (changed) speed::Set(t.name, p);
                    if (ImGuiMCP::Button("Save")) speed::Save(t.name);
                    ImGuiMCP::SameLine();
                    if (ImGuiMCP::Button(t.builtIn ? "Reset to CB's defaults" : "Reset to the record")) speed::Reset(t.name);
                }
                ImGuiMCP::PopID();
            }
        }

        void Register()
        {
            if (!SKSEMenuFramework::IsInstalled()) {
                LOG_INFO("config: SKSE Menu Framework is not installed - no in-game config.");
                return;
            }
            /// @note The header took the module handle at static init, before SKSE loaded SMF (it loads after CB).
            menuFramework = GetModuleHandleW(L"SKSEMenuFramework");
            if (!menuFramework) {
                LOG_WARN("config: SKSE Menu Framework's DLL is not loaded - no in-game config.");
                return;
            }
            SKSEMenuFramework::SetSection("Community Behaviors");
            SKSEMenuFramework::AddSectionItem("Movement Speeds", RenderSpeeds);
            LOG_INFO("config: registered (SKSE Menu Framework > Community Behaviors > Movement Speeds).");
        }

        [[maybe_unused]] const bool s_registered = platform::startup::OnDataLoaded(&Register, "config menu");

    }  // namespace

}  // namespace CB::feature::controller::config
