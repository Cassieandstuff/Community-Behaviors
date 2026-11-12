#include "PCH.h"

#include "CharDebugHud.h"
#include "CharFacing.h"
#include "CharMovementTuning.h"
#include "CharSpeedProfile.h"

#include <features/Advanced Dodge/Dodge.h>
#include <features/Advanced Input/Input.h>
#include <platform/startup/Startup.h>

#include <SKSEMenuFramework.h>

#include <format>
#include <string>

/// @file
/// @brief The in-game config prototype: SKSE Menu Framework > Community Behaviors > Movement Speeds, editing the speed
///        profile (CharSpeedProfile) live and saving it to settings.ini; also Movement Tuning and Advanced Dodge.
/// @warning The only translation unit that includes SKSEMenuFramework.h: the header's module handle is a per-TU static,
///          and every ImGui wrapper and registration call reads this TU's copy.

namespace CB::feature::controller::config {

    namespace {

        namespace loco = hky::locomotion;

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

        /// @brief The movement model's numbers, live; Save writes settings.ini [Movement].
        void __stdcall RenderTuning()
        {
            ImGuiMCP::TextWrapped("How the player and the NPCs CB drives accelerate, brake, turn and grip (GASP's character movement "
                                  "numbers). Changes apply at once; Save writes settings.ini [Movement].");
            ImGuiMCP::Separator();
            loco::Profile        p        = tuning::Current();
            const loco::Profile  defaults = loco::kPlayer;
            bool                 changed  = false;
            for (const tuning::Field& f : tuning::Fields()) {
                float shown = p.*f.member * f.shown;
                const std::string label(f.label);
                if (ImGuiMCP::DragFloat(label.c_str(), &shown, f.max / 500.0f, 0.0f, f.max, "%.3g")) {
                    p.*f.member = (std::max)(0.0f, shown) / f.shown;
                    changed     = true;
                }
                ImGuiMCP::SameLine();
                ImGuiMCP::TextDisabled("%.*s, GASP %.3g", static_cast<int>(f.unit.size()), f.unit.data(), defaults.*f.member * f.shown);
            }
            if (changed) tuning::Set(p);
            if (ImGuiMCP::Button("Save")) tuning::Save();
            ImGuiMCP::SameLine();
            if (ImGuiMCP::Button("Reset to GASP's")) tuning::Reset();
        }

        /// @brief Which dodge key the Advanced Dodge page is waiting for, and the input frame the wait began on.
        /// @note  Render thread only.
        enum class Capture { None, KeyboardOrMouse, Gamepad };
        Capture       g_capture      = Capture::None;
        std::uint64_t g_captureFrame = 0;

        std::string KeyName(input::Key k)
        {
            if (k.device == input::Device::Mouse) return std::format("mouse button {}", k.code + 1);
            if (k.code == 0) return "none";
            return k.device == input::Device::Gamepad ? std::format("gamepad 0x{:X}", k.code) : std::format("scan code 0x{:02X}", k.code);
        }

        /// @brief A key row: its binding, a button that waits for the next press on its kind of device, and a clear.
        bool EditKey(const char* label, input::Key& key, Capture kind)
        {
            ImGuiMCP::PushID(label);
            bool changed = false;
            if (g_capture == kind) {
                input::Key    pressed;
                std::uint64_t frame = 0;
                const bool    pad   = kind == Capture::Gamepad;
                if (input::LastPress(pressed, frame) && frame > g_captureFrame &&
                    (pressed.device == input::Device::Gamepad) == pad && pressed.device != input::Device::VirtualKeyboard) {
                    key       = pressed;
                    changed   = true;
                    g_capture = Capture::None;
                }
                ImGuiMCP::Text("%s: press a %s...", label, pad ? "gamepad button" : "key or mouse button");
                ImGuiMCP::SameLine();
                if (ImGuiMCP::Button("Cancel")) g_capture = Capture::None;
            } else {
                ImGuiMCP::Text("%s: %s", label, KeyName(key).c_str());
                ImGuiMCP::SameLine();
                if (ImGuiMCP::Button("Bind")) {
                    g_capture      = kind;
                    g_captureFrame = input::Frame();
                }
                ImGuiMCP::SameLine();
                if (ImGuiMCP::Button("Clear")) {
                    key     = { kind == Capture::Gamepad ? input::Device::Gamepad : input::Device::Keyboard, 0 };
                    changed = true;
                }
            }
            ImGuiMCP::PopID();
            return changed;
        }

        /// @brief Advanced Dodge's settings, live; Save writes settings.ini [Dodge].
        void __stdcall RenderDodge()
        {
            ImGuiMCP::TextWrapped("How the player asks to dodge and what it costs. The dodge goes where the move input points, "
                                  "relative to the camera, as one of 8 ways around the body (no input: a backstep). Changes apply "
                                  "at once; Save writes settings.ini [Dodge]. Needs bAdvancedDodge and bAdvancedInput.");
            ImGuiMCP::Text("Last: %s", dodge::Status().c_str());
            ImGuiMCP::Separator();

            dodge::Settings s       = dodge::Current();
            bool            changed = false;

            static const char* const kTriggers[] = { "Tap Sprint (hold to sprint)", "Dodge key", "Double tap a direction" };
            int                      trigger     = static_cast<int>(s.trigger);
            if (ImGuiMCP::Combo("Input", &trigger, kTriggers, 3)) {
                s.trigger = static_cast<loco::DodgeTrigger>(trigger);
                changed   = true;
            }
            switch (s.trigger) {
            case loco::DodgeTrigger::SprintTap:
                changed |= ImGuiMCP::SliderFloat("Longest tap (s)", &s.tapMaxHold, 0.05f, 1.0f, "%.2f");
                ImGuiMCP::TextDisabled("Sprint held longer than this sprints; needs bMovementModel (CB's sprint rules).");
                break;
            case loco::DodgeTrigger::DedicatedKey:
                changed |= EditKey("Keyboard / mouse", s.keyboardKey, Capture::KeyboardOrMouse);
                changed |= EditKey("Gamepad", s.gamepadKey, Capture::Gamepad);
                break;
            case loco::DodgeTrigger::DoubleTap:
                changed |= ImGuiMCP::SliderFloat("Double tap window (s)", &s.doubleTapWindow, 0.05f, 1.0f, "%.2f");
                ImGuiMCP::TextDisabled("Two pushes the same way, each from rest (keys or stick).");
                break;
            }

            ImGuiMCP::SeparatorText("Stamina");
            static const char* const kModes[] = { "Points", "Percent of maximum" };
            int                      mode     = static_cast<int>(s.stamina.mode);
            if (ImGuiMCP::Combo("Cost is", &mode, kModes, 2)) {
                s.stamina.mode = static_cast<loco::DodgeCostMode>(mode);
                changed        = true;
            }
            changed |= ImGuiMCP::DragFloat("Cost", &s.stamina.base, 0.5f, 0.0f, s.stamina.mode == loco::DodgeCostMode::Flat ? 500.0f : 100.0f, "%.1f");
            changed |= ImGuiMCP::Checkbox("Needs the full cost (off: any stamina left will do)", &s.stamina.requireFull);

            if (changed) dodge::Set(s);
            ImGuiMCP::Separator();
            if (ImGuiMCP::Button("Save")) dodge::Save();
        }

        /// @brief The Debug category's character controller page: the debug HUD's switch.
        void __stdcall RenderDebugController()
        {
            bool on = hud::On();
            if (ImGuiMCP::Checkbox("Character controller debug HUD", &on)) hud::SetOn(on);
            ImGuiMCP::TextDisabled("Draws the player's capsule, floor rays, velocities, predicted path and motion matching state.");
            ImGuiMCP::TextDisabled("Saved to settings.ini [Character] bDebugHud.");
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
            SKSEMenuFramework::AddSectionItem("Movement Tuning", RenderTuning);
            SKSEMenuFramework::AddSectionItem("Advanced Dodge", RenderDodge);
            SKSEMenuFramework::AddSectionItem("Debug/Character Controller", RenderDebugController);
            LOG_INFO("config: registered (SKSE Menu Framework > Community Behaviors > Movement Speeds, Movement Tuning, Advanced Dodge, Debug/Character Controller).");
        }

        [[maybe_unused]] const bool s_registered = platform::startup::OnDataLoaded(&Register, "config menu");

    }  // namespace

}  // namespace CB::feature::controller::config
