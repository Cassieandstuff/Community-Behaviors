#include "PCH.h"

#include "CharMovementModel.h"
#include "CharSprint.h"

#include <platform/game/Player.h>
#include <platform/hooks/Override.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <mutex>

/// @file
/// @brief The player's sprint under the movement model, replacing vanilla's per-frame evaluator: the key's toggle
///        (PlayerFlags::isSprinting) stays the player's intent; CB suspends it while strafing past the model's sprint
///        cone, blocking or airborne (and restores it after), cancels it on stopping, empty stamina, over-encumbrance or
///        an attack, and asks the engine for SprintStart / SprintStop exactly as vanilla does. Also: jumping while
///        sprinting, and no sprint stamina drain out of combat.
/// @note Vanilla's ~30 deg prevMoveVec cone is not applied: the model's direction rule (SprintDirectionAllowed) replaces
///       it, and CB's forward-only move input would pass it anyway.

namespace CB::feature::controller::sprint {

    namespace {

        namespace hooks = platform::hooks;
        namespace game  = platform::game;
        using Evaluate  = hooks::Override<&game::PlayerCharacter_UpdateSprintRequest>;
        using Jump      = hooks::Override<&game::JumpHandler_ProcessButton>;
        using Update    = hooks::Override<&game::Actor_UpdateSprint>;
        using Drain     = hooks::Override<&game::Actor_SprintStaminaDrain>;

        /// @brief Below this speed (game units/s) with no stick the player has stopped: the sprint is cancelled.
        constexpr float  kStillSpeed = 10.0f;
        /// @brief Evaluations further apart than this (a load screen) drop a suspend: vanilla clears the toggle on a
        ///        cell change (the load's move), which CB could not tell from its own clear. Current goes stale after it too.
        constexpr double kStale          = 0.5;
        constexpr int    kPriorityAction = 2;   ///< vanilla's priority for SprintStart / SprintStop

        std::atomic<int> g_logs{ 0 };
        bool LogBudget() { return g_logs.fetch_add(1, std::memory_order_relaxed) < 200; }

        double Now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

        /// @brief The rules' memory between evaluations.
        /// @note Written only by the evaluator, which runs on one thread at a time (the player's update).
        struct Rules {
            bool        suspended = false;   ///< CB cleared the toggle for a suspend reason and will set it back
            bool        lastBit   = false;   ///< the toggle as CB left it, to see what changed it since
            double      lastRun   = -1.0e9;
            const void* place     = nullptr;   ///< the interior cell, or the exterior's worldspace
        };
        Rules g_rules;

        struct Published {
            std::mutex lock;
            State      state;
            bool       valid = false;
            double     at    = -1.0e9;
        };
        Published g_published;

        void Publish(bool valid, const State& state = {})
        {
            std::scoped_lock lock(g_published.lock);
            g_published.valid = valid;
            g_published.state = state;
            g_published.at    = Now();
        }

        /// @brief Where the player is, coarsely: a teleport (door, fast travel, coc) changes it, walking across exterior
        ///        cells does not.
        const void* PlaceOf(RE::PlayerCharacter* player)
        {
            RE::TESObjectCELL* cell = player->GetParentCell();
            if (cell && cell->IsInteriorCell()) return cell;
            if (RE::TESWorldSpace* world = player->GetWorldspace()) return world;
            return cell;
        }

        bool InAir(RE::PlayerCharacter* player, const move::Snapshot& snap)
        {
            if (snap.input.airborne) return true;
            const RE::bhkCharacterController* ctrl = player->GetCharController();
            return ctrl && ctrl->context.currentState == RE::hkpCharacterStateTypes::kInAir;
        }

        /// @note Vanilla's sync check: an attack ends the sprint unless the graph is sync-sprinting (a sprint attack).
        bool Attacking(RE::PlayerCharacter* player)
        {
            return player->AsActorState()->GetAttackState() != RE::ATTACK_STATE_ENUM::kNone &&
                   !game::PlayerCharacter_IsSyncSprintState(player);
        }

        const char* CancelReason(RE::PlayerCharacter* player, const move::Snapshot& snap)
        {
            const float speed = std::sqrt(snap.state.vx * snap.state.vx + snap.state.vy * snap.state.vy);
            if (!snap.input.haveStick && speed < kStillSpeed) return "stopped";
            if (player->AsActorValueOwner()->GetActorValue(RE::ActorValue::kStamina) <= 0.0f) return "stamina";
            if (player->IsOverEncumbered()) return "encumbered";
            if (Attacking(player)) return "attacking";
            return "";
        }

        const char* SuspendReason(RE::PlayerCharacter* player, const move::Snapshot& snap)
        {
            if (!move::SprintDirectionAllowed(snap.state, snap.input)) return "lock beyond 50 deg";
            if (player->IsBlocking()) return "blocking";
            if (InAir(player, snap)) return "airborne";
            return "";
        }

        void OnEvaluate(Evaluate::Next next, RE::PlayerCharacter* player)
        {
            move::Snapshot snap;
            RE::NiPointer<RE::Actor> controlling;
            const bool               mine = move::Current(snap) && player == RE::PlayerCharacter::GetSingleton() &&
                                             player->GetControllingActor(controlling) && controlling.get() == player;
            if (!mine) {
                /// @note Diagnostic: why vanilla's evaluator ran instead of CB's rules, each time the reason changes.
                static const char* lastWhy = "";
                const char* why = !move::Current(snap) ? "no fresh movement model" : player != RE::PlayerCharacter::GetSingleton() ? "not the player"
                                                                                                                    : "controlled by a mount";
                if (why != lastWhy && LogBudget()) LOG_INFO("sprint: vanilla's rules this frame ({})", why);
                lastWhy = why;
                g_rules.suspended = false;
                Publish(false);
                return next(player);
            }

            Rules&       r    = g_rules;
            const double now  = Now();
            const void*  here = PlaceOf(player);
            if (r.suspended && (now - r.lastRun > kStale || here != r.place)) {
                r.suspended = false;
                if (LogBudget()) LOG_INFO("sprint: suspend dropped (load or teleport)");
            }
            r.lastRun = now;
            r.place   = here;

            auto& flags = player->GetPlayerFlags();
            bool  bit   = flags.isSprinting;
            if (r.suspended && bit && !r.lastBit) {
                bit         = false;
                r.suspended = false;
                if (LogBudget()) LOG_INFO("sprint: toggled off while suspended");
            }

            const bool  wanted = bit || r.suspended;
            const char* why    = "";
            if (wanted) {
                const char* cancel  = CancelReason(player, snap);
                const char* suspend = SuspendReason(player, snap);
                if (*cancel) {
                    why = cancel;
                    if (LogBudget()) LOG_INFO("sprint: cancelled ({})", cancel);
                    bit         = false;
                    r.suspended = false;
                } else if (*suspend) {
                    why = suspend;
                    if (bit) {
                        if (LogBudget()) LOG_INFO("sprint: suspended ({})", suspend);
                        bit         = false;
                        r.suspended = true;
                    }
                } else if (r.suspended) {
                    if (LogBudget()) LOG_INFO("sprint: resumed");
                    bit         = true;
                    r.suspended = false;
                }
            }
            flags.isSprinting = bit;
            r.lastBit         = bit;

            /// @note Vanilla's gait gate, kept as a gate only: walking with the toggle on holds it rather than cancelling.
            const bool gait    = player->AsActorState()->IsSprinting() || player->IsRunning();
            const bool allowed = bit && snap.input.haveStick && gait;
            if (allowed != game::PlayerCharacter_IsSyncSprintState(player)) {
                if (LogBudget())
                    LOG_INFO("sprint: {} (toggle {}, stick {}, gait gate {}, why '{}')", allowed ? "start" : "stop", bit, snap.input.haveStick, gait, why);
                game::PlayerControls_DoAction(RE::PlayerControls::GetSingleton(),
                                              allowed ? game::kActionSprintStart : game::kActionSprintStop, kPriorityAction);
            }
            Publish(true, State{ wanted && (bit || r.suspended), r.suspended, allowed, why });
        }

        [[maybe_unused]] const bool s_evaluate =
            Evaluate::Subscribe(&OnEvaluate, hooks::Priority::Normal, "CharSprint", "char_movement_model");

        std::atomic<int> g_jumpLogs{ 0 };

        /// @note Lifts only the sprint refusal: every other branch (a flying mount, a mount, no kCanJump, a release) is
        ///       vanilla's.
        void OnJump(Jump::Next next, RE::JumpHandler* handler, RE::ButtonEvent* event, RE::PlayerControlsData* data)
        {
            auto* player = RE::PlayerCharacter::GetSingleton();
            const bool press = event && event->Value() != 0.0f && event->HeldDuration() == 0.0f;
            if (!player || !press || player->IsOnMount()) return next(handler, event, data);
            const RE::bhkCharacterController* ctrl = player->GetCharController();
            if (!ctrl || !ctrl->flags.all(RE::CHARACTER_FLAGS::kCanJump)) return next(handler, event, data);
            const bool syncSprint = game::PlayerCharacter_IsSyncSprintState(player);
            const bool sprinting  = player->AsActorState()->IsSprinting();
            if (!syncSprint && !sprinting) return next(handler, event, data);
            if (g_jumpLogs.fetch_add(1, std::memory_order_relaxed) < 5)
                LOG_INFO("sprint: jump while sprinting (sync {}, flag {})", syncSprint, sprinting);
            game::Actor_Jump(player);
        }

        [[maybe_unused]] const bool s_jump = Jump::Subscribe(&OnJump, hooks::Priority::Normal, "CharSprint", "char_movement_model");

        /// @brief Set while Actor_UpdateSprint runs for the player out of combat: its drain is skipped.
        thread_local bool t_free = false;
        std::atomic<bool> g_loggedFree{ false };

        void OnUpdate(Update::Next next, RE::Actor* actor)
        {
            auto* player = RE::PlayerCharacter::GetSingleton();
            if (!player || actor != player || player->IsInCombat()) return next(actor);
            struct Mark {
                Mark() { t_free = true; }
                ~Mark() { t_free = false; }
            } mark;
            next(actor);
        }

        float OnDrain(Drain::Next next, float equippedWeight, float deltaTime)
        {
            if (!t_free) return next(equippedWeight, deltaTime);
            bool expected = false;
            if (g_loggedFree.compare_exchange_strong(expected, true)) LOG_INFO("sprint: out of combat - no stamina drain");
            return 0.0f;
        }

        [[maybe_unused]] const bool s_update =
            Update::Subscribe(&OnUpdate, hooks::Priority::Normal, "CharSprint", "char_sprint_free_out_of_combat");
        [[maybe_unused]] const bool s_drain =
            Drain::Subscribe(&OnDrain, hooks::Priority::Normal, "CharSprint", "char_sprint_free_out_of_combat");

    }  // namespace

    bool Current(State& out)
    {
        std::scoped_lock lock(g_published.lock);
        if (!g_published.valid || Now() - g_published.at > kStale) return false;
        out = g_published.state;
        return true;
    }

}  // namespace CB::feature::controller::sprint
