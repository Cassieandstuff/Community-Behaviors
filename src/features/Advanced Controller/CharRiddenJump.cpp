#include "PCH.h"

#include "CharGround.h"

#include <platform/game/Player.h>
#include <platform/hooks/Override.h>

#include <atomic>
#include <chrono>

/// @file
/// @brief The ridden horse's jump launches on the press (horse_movement_model, spec H11), like the player's: the jump
///        key still starts the horse graph's jump (Actor_Jump), and its controller is asked to jump at once instead of
///        when the clip's jump event arrives; that later request is dropped.
/// @note Only while CB's ground layer manages the horse (on the proxy, ridden): its ground states take the request with
///       the player's jump rules (launch from the commanded velocity, coyote window, gravity). Off it, vanilla's
///       clip-timed launch stands.

namespace CB::feature::controller {

    namespace {

        namespace hooks = platform::hooks;
        namespace game  = platform::game;
        using JumpKey   = hooks::Override<&game::JumpHandler_ProcessButton>;
        using JumpEvent = hooks::Override<&game::JumpAnimEventHandler_Execute>;

        /// @brief How long after a press-launch the clip's own jump request is dropped: past the clip's event (0.467 s)
        ///        with room for a slow transition into it.
        constexpr double kEventWindow = 1.5;

        std::atomic<double> g_launchedAt{ -1.0e9 };
        std::atomic<int>    g_logs{ 0 };

        double Now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

        float GmstFloat(const char* name, float def)
        {
            auto*        gmst = RE::GameSettingCollection::GetSingleton();
            RE::Setting* s    = gmst ? gmst->GetSetting(name) : nullptr;
            return s ? s->GetFloat() : def;
        }

        /// @brief The ridden horse's controller while CB's ground layer manages it; null otherwise.
        hk::bhkCharacterController* ManagedHorse(RE::NiPointer<RE::Actor>& horse)
        {
            auto* player = RE::PlayerCharacter::GetSingleton();
            if (!player || !player->IsOnMount() || !player->GetMount(horse) || !horse) return nullptr;
            auto* ctrl = reinterpret_cast<hk::bhkCharacterController*>(horse->GetCharController());
            return ctrl && IsManaged(*ctrl) && !IsPlayer(ctrl) ? ctrl : nullptr;
        }

        /// @note Vanilla's handler runs first (the horse graph's jump action); the controller request follows only for a
        ///       horse that can jump now (kCanJump) and that its graph is not moving.
        void OnJumpKey(JumpKey::Next next, RE::JumpHandler* handler, RE::ButtonEvent* event, RE::PlayerControlsData* data)
        {
            next(handler, event, data);
            const bool press = event && event->Value() != 0.0f && event->HeldDuration() == 0.0f;
            if (!press) return;
            RE::NiPointer<RE::Actor>    horse;
            hk::bhkCharacterController* ctrl = ManagedHorse(horse);
            if (!ctrl || !(ctrl->m_flags & kCanJump) || *GraphMovesActor(horse.get())) return;
            static const float height = GmstFloat("fJumpHeightMin", 64.0f);
            constexpr float    kHavokPerGame = 1.0f / 69.99125f;
            ctrl->m_jumpHeight = height * horse->GetScale() * kHavokPerGame;
            ctrl->m_wantState  = hk::HK_CHARACTER_JUMPING;
            g_launchedAt.store(Now(), std::memory_order_relaxed);
            if (g_logs.fetch_add(1, std::memory_order_relaxed) < 10)
                LOG_INFO("ridden jump: the horse jumps on the press ({:.0f} units high)", height * horse->GetScale());
        }

        /// @note Drops the clip's request for the managed horse after a press-launch; any other actor's is vanilla's.
        bool OnJumpEvent(JumpEvent::Next next, void* handler, RE::Actor* actor, const void* parameter)
        {
            RE::NiPointer<RE::Actor> horse;
            if (actor && ManagedHorse(horse) && horse.get() == actor && Now() - g_launchedAt.load(std::memory_order_relaxed) <= kEventWindow) {
                if (g_logs.fetch_add(1, std::memory_order_relaxed) < 10) LOG_INFO("ridden jump: the clip's jump event dropped (launched on the press)");
                return true;
            }
            return next(handler, actor, parameter);
        }

        [[maybe_unused]] const bool s_key   = JumpKey::Subscribe(&OnJumpKey, hooks::Priority::Late, "CharRiddenJump", "horse_movement_model");
        [[maybe_unused]] const bool s_event = JumpEvent::Subscribe(&OnJumpEvent, hooks::Priority::Normal, "CharRiddenJump", "horse_movement_model");

    }  // namespace

}  // namespace CB::feature::controller
