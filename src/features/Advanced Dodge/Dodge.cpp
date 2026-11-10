#include "PCH.h"

#include "Dodge.h"

#include <core/debug/Settings.h>
#include <features/Camera/Camera.h>
#include <locomotion/Movement.h>
#include <platform/hooks/CallSite.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <format>
#include <mutex>

/// @file
/// @brief The dodge input behind Dodge.h, run at PollInputDevices' dispatch after Advanced Input recorded the frame and
///        the game's sinks (PlayerControls) rebuilt the move input from it.

namespace CB::feature::dodge {

    namespace {

        namespace hooks = CB::platform::hooks;
        namespace loco  = CB::engine::locomotion;
        namespace dbg   = CB::core::debug;

        using Dispatch = hooks::CallSite<hooks::engine::PollInputDevices_DispatchCall, void(RE::BSInputDeviceManager*, RE::InputEvent* const*)>;

        constexpr std::string_view kSection = "Dodge";
        /// @brief SKSE's key code convention for settings.ini: scan codes below it, mouse buttons from it.
        constexpr std::uint32_t    kMouseBase = 256;
        constexpr float            kRadToDeg  = 57.2957795f;

        double Now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

        float ReadFloat(std::string_view key, float def)
        {
            const std::string v = dbg::Value(kSection, key);
            if (v.empty()) return def;
            char*       end = nullptr;
            const float f   = std::strtof(v.c_str(), &end);
            return end != v.c_str() && std::isfinite(f) ? f : def;
        }

        long ReadInt(std::string_view key, long def)
        {
            const std::string v = dbg::Value(kSection, key);
            if (v.empty()) return def;
            char*      end = nullptr;
            const long i   = std::strtol(v.c_str(), &end, 0);
            return end != v.c_str() ? i : def;
        }

        bool ReadBool(std::string_view key, bool def)
        {
            const std::string v = dbg::Value(kSection, key);
            if (v == "1" || v == "true" || v == "True") return true;
            if (v == "0" || v == "false" || v == "False") return false;
            return def;
        }

        Settings Load()
        {
            Settings s;
            s.trigger         = static_cast<loco::DodgeTrigger>(std::clamp<long>(ReadInt("iTrigger", 0), 0, 2));
            s.tapMaxHold      = std::clamp(ReadFloat("fTapMaxHold", s.tapMaxHold), 0.05f, 1.0f);
            s.doubleTapWindow = std::clamp(ReadFloat("fDoubleTapWindow", s.doubleTapWindow), 0.05f, 1.0f);
            const long key    = (std::max)(0L, ReadInt("iKeyboardKey", 0));
            s.keyboardKey     = static_cast<std::uint32_t>(key) >= kMouseBase
                                    ? input::Key{ input::Device::Mouse, static_cast<std::uint32_t>(key) - kMouseBase }
                                    : input::Key{ input::Device::Keyboard, static_cast<std::uint32_t>(key) };
            s.gamepadKey      = { input::Device::Gamepad, static_cast<std::uint32_t>((std::max)(0L, ReadInt("iGamepadKey", 0))) };
            s.stamina.mode    = static_cast<loco::DodgeCostMode>(std::clamp<long>(ReadInt("iStaminaMode", 0), 0, 1));
            s.stamina.base    = (std::max)(0.0f, ReadFloat("fStaminaCost", s.stamina.base));
            s.stamina.requireFull = ReadBool("bRequireFullStamina", s.stamina.requireFull);
            return s;
        }

        std::mutex  g_lock;
        Settings    g_settings;
        bool        g_loaded = false;
        std::string g_status = "no dodge yet";

        Settings Snapshot()
        {
            std::scoped_lock lock(g_lock);
            if (!g_loaded) {
                g_settings = Load();
                g_loaded   = true;
            }
            return g_settings;
        }

        void SetStatus(std::string s)
        {
            LOG_INFO("dodge: {}", s);
            std::scoped_lock lock(g_lock);
            g_status = std::move(s);
        }

        std::atomic<bool> g_sprintMode{ false };
        std::atomic<bool> g_sprintHeld{ false };

        /// @note Touched only from the dispatch (the main thread).
        loco::TapHold   g_tap;
        loco::DoubleTap g_double;

        /// @brief Whether a key bound to Sprint is down on any device (the player's own bindings, ControlMap).
        bool SprintDown()
        {
            auto* map    = RE::ControlMap::GetSingleton();
            auto* events = RE::UserEvents::GetSingleton();
            if (!map || !events) return false;
            constexpr input::Device devices[] = { input::Device::Keyboard, input::Device::Mouse, input::Device::Gamepad };
            for (const input::Device d : devices) {
                const std::uint32_t code = map->GetMappedKey(events->sprint, static_cast<RE::INPUT_DEVICE>(d));
                if (code != RE::ControlMap::kInvalid && input::IsDown({ d, code })) return true;
            }
            return false;
        }

        bool Bound(input::Key k) { return k.code != 0 || k.device == input::Device::Mouse; }

        bool InAir(RE::PlayerCharacter* player)
        {
            const RE::bhkCharacterController* ctrl = player->GetCharController();
            return ctrl && ctrl->context.currentState == RE::hkpCharacterStateTypes::kInAir;
        }

        /// @brief Why the player may not dodge now, or "".
        const char* Refusal(RE::PlayerCharacter* player)
        {
            static const RE::BSFixedString isDodging("AD_IsDodging");
            auto*                          ui  = RE::UI::GetSingleton();
            auto*                          map = RE::ControlMap::GetSingleton();
            if (!player->Is3DLoaded()) return "no 3D";
            if (ui && ui->GameIsPaused()) return "a menu is open";
            if (map && !map->IsMovementControlsEnabled()) return "movement controls are disabled";
            if (player->IsOnMount()) return "mounted";
            const RE::ActorState* state = player->AsActorState();
            if (state->IsSwimming()) return "swimming";
            if (state->GetSitSleepState() != RE::SIT_SLEEP_STATE::kNormal) return "sitting or sleeping";
            if (state->GetKnockState() != RE::KNOCK_STATE_ENUM::kNormal) return "knocked down";
            if (state->GetFlyState() != RE::FLY_STATE::kNone) return "flying";
            if (state->IsBleedingOut()) return "bleeding out";
            if (InAir(player)) return "airborne";
            bool dodging = false;
            if (player->GetGraphVariableBool(isDodging, dodging) && dodging) return "already dodging (AD_IsDodging)";
            return "";
        }

        void Dodge(RE::PlayerCharacter* player, RE::NiPoint2 move, const Settings& s, const char* trigger)
        {
            static const RE::BSFixedString event("AD_Dodge");
            static const RE::BSFixedString direction("AD_Direction");
            static const RE::BSFixedString angle("AD_Angle");

            if (const char* why = Refusal(player); *why) return SetStatus(std::format("{}: refused, {}", trigger, why));

            /// @note Without CB's camera the move input is read relative to the body.
            const float body = player->GetAngleZ();
            float       yaw  = body;
            if (!camera::ControlYaw(yaw)) yaw = body;
            float      heading = 0.0f;
            const bool have    = loco::StickHeadingOf(move.x, move.y, yaw, heading);
            const auto pick    = loco::PickDodgeDirection(have, heading, body);

            RE::ActorValueOwner* av   = player->AsActorValueOwner();
            const float          cur  = av->GetActorValue(RE::ActorValue::kStamina);
            const float          cost = loco::DodgeStaminaCost(s.stamina, player->GetActorValueMax(RE::ActorValue::kStamina), 1.0f);
            if (!loco::DodgeAffordable(s.stamina, cur, cost))
                return SetStatus(std::format("{}: refused, stamina {:.0f} < cost {:.0f}", trigger, cur, cost));

            static constexpr const char* kNames[] = { "N", "F", "FR", "R", "BR", "B", "BL", "L", "FL" };
            const char*                  name     = kNames[static_cast<int>(pick.direction)];
            const float                  degrees  = pick.angle * kRadToDeg;
            player->SetGraphVariableInt(direction, static_cast<std::int32_t>(pick.direction));
            player->SetGraphVariableFloat(angle, degrees);
            if (!player->NotifyAnimationGraph(event))
                return SetStatus(std::format("{}: {} ({:.0f} deg), no graph answered AD_Dodge - nothing charged", trigger, name, degrees));
            if (cost > 0.0f) av->DamageActorValue(RE::ActorValue::kStamina, cost);
            SetStatus(std::format("{}: dodge {} ({:.0f} deg), stamina {:.0f} - {:.0f}", trigger, name, degrees, cur, cost));
        }

        void Tick()
        {
            /// @note Without Advanced Input's buffer no key ever reads down.
            static const bool haveInput = [] {
                const bool on = dbg::FlagOn("adv_input");
                if (!on) LOG_WARN("dodge: bAdvancedDodge needs bAdvancedInput (its key buffer) - no dodge input");
                return on;
            }();
            auto* player = RE::PlayerCharacter::GetSingleton();
            if (!haveInput || !player) return;
            const Settings     s        = Snapshot();
            const double       now      = Now();
            auto*              controls = RE::PlayerControls::GetSingleton();
            const RE::NiPoint2 move     = controls ? controls->data.moveInputVec : RE::NiPoint2();

            g_sprintMode.store(s.trigger == loco::DodgeTrigger::SprintTap, std::memory_order_relaxed);
            switch (s.trigger) {
            case loco::DodgeTrigger::SprintTap: {
                const auto r = g_tap.Step(SprintDown(), now, s.tapMaxHold);
                g_sprintHeld.store(r.holding, std::memory_order_relaxed);
                if (r.tapped) Dodge(player, move, s, "sprint tap");
                break;
            }
            case loco::DodgeTrigger::DedicatedKey:
                if ((Bound(s.keyboardKey) && input::Pressed(s.keyboardKey)) || (Bound(s.gamepadKey) && input::Pressed(s.gamepadKey)))
                    Dodge(player, move, s, "dodge key");
                break;
            case loco::DodgeTrigger::DoubleTap:
                if (g_double.Step(move.x, move.y, now, s.doubleTapWindow)) Dodge(player, move, s, "double tap");
                break;
            }
        }

        /// @note After `next`: PlayerControls rebuilds moveInputVec from zero inside the dispatch, so this frame's move
        ///       input (keys and stick alike) is only there afterwards.
        void OnDispatch(Dispatch::Next next, RE::BSInputDeviceManager* manager, RE::InputEvent* const* events)
        {
            next(manager, events);
            Tick();
        }

        [[maybe_unused]] const bool s_dispatch = Dispatch::Subscribe(&OnDispatch, hooks::Priority::Normal, "Advanced Dodge", "adv_dodge");

    }  // namespace

    Settings Current() { return Snapshot(); }

    void Set(const Settings& s)
    {
        std::scoped_lock lock(g_lock);
        g_settings = s;
        g_loaded   = true;
    }

    bool Save()
    {
        const Settings      s   = Snapshot();
        const std::uint32_t key = s.keyboardKey.device == input::Device::Mouse ? kMouseBase + s.keyboardKey.code : s.keyboardKey.code;
        bool                ok  = true;
        ok &= dbg::SetValue(kSection, "iTrigger", std::to_string(static_cast<int>(s.trigger)));
        ok &= dbg::SetValue(kSection, "fTapMaxHold", std::format("{:.3f}", s.tapMaxHold));
        ok &= dbg::SetValue(kSection, "fDoubleTapWindow", std::format("{:.3f}", s.doubleTapWindow));
        ok &= dbg::SetValue(kSection, "iKeyboardKey", std::to_string(key));
        ok &= dbg::SetValue(kSection, "iGamepadKey", std::format("0x{:X}", s.gamepadKey.code));
        ok &= dbg::SetValue(kSection, "iStaminaMode", std::to_string(static_cast<int>(s.stamina.mode)));
        ok &= dbg::SetValue(kSection, "fStaminaCost", std::format("{:.2f}", s.stamina.base));
        ok &= dbg::SetValue(kSection, "bRequireFullStamina", s.stamina.requireFull ? "true" : "false");
        return ok;
    }

    bool SprintHoldIntent(bool& held)
    {
        if (!g_sprintMode.load(std::memory_order_relaxed)) return false;
        held = g_sprintHeld.load(std::memory_order_relaxed);
        return true;
    }

    std::string Status()
    {
        std::scoped_lock lock(g_lock);
        return g_status;
    }

}  // namespace CB::feature::dodge
