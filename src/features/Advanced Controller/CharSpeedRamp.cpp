#include "PCH.h"

#include "CharDebugHud.h"
#include "CharGround.h"

#include <core/debug/Settings.h>
#include <platform/hooks/hookslib.h>

#include <algorithm>
#include <atomic>
#include <cstdint>

/// @file
/// @brief Speed acceleration for the player: ramps the speed the movement controller produces each frame.
/// @note Why here and not in the character controller: vanilla's player path has no acceleration (the speed jumps
///       to its target; PlayerControls only limits a drop of more than 500 game units/s in one call), and the same
///       speed feeds the graph's `Speed` (what BSSpeedSamplerModifier fits the locomotion blend to) and the frame
///       movement physics integrates. Ramping only the physics would play full speed over a body still speeding
///       up; ramping this one number keeps animation and physics together.
/// @note One ramp time for every movement type: MOVT records carry speeds and anim-change thresholds but no
///       acceleration, so the acceleration is the target over fRampTime and each MOVT reaches its own speed in
///       the same time.

namespace CB::feature {

    namespace {

        namespace hooks = platform::hooks;
        namespace ch    = CB::feature::controller;
        using Frame     = hooks::MovementControllerFrame;

        /// @brief Rises smaller than this (game units/s) pass at once: steering noise, not a speed change.
        constexpr float kRampThreshold = 5.0f;

        float RampTime()
        {
            static const float t = [] {
                const float v = std::clamp(ch::SettingFloat("fRampTime", 0.4f), 0.01f, 5.0f);
                LOG_INFO("speed ramp: any rise reaches its target speed in {:.2f} s", v);
                return v;
            }();
            return t;
        }

        /// @brief The player's ramp between frames.
        /// @note Only the player's controller reaches it, from one thread at a time, so it is plain state.
        struct Ramp {
            bool  init     = false;
            bool  ramping  = false;
            float speed    = 0.0f;   ///< the speed handed on last frame (game units/s)
            float from     = 0.0f;
            float rampTime = 0.0f;
        };
        Ramp              g_ramp;
        std::atomic<bool> g_loggedTargetSpeed{ false };
        std::atomic<int>  g_logs{ 0 };

        bool LogBudget() { return g_logs.fetch_add(1, std::memory_order_relaxed) < 2000; }

        RE::Actor* PlayerOf(void* controller)
        {
            auto* player = RE::PlayerCharacter::GetSingleton();
            if (!player) return nullptr;
            const auto& mc = player->GetActorRuntimeData().movementController;
            return mc && static_cast<void*>(mc.get()) == controller ? player : nullptr;
        }

        /// @brief Whether the graph drives the speed itself (animation-driven, speed-synced or synced): left alone.
        bool GraphDrivesSpeed(RE::Actor* actor)
        {
            static const RE::BSFixedString animationDriven("bAnimationDriven");
            static const RE::BSFixedString speedSynced("bSpeedSynced");
            static const RE::BSFixedString isSynced("bIsSynced");
            bool on = false;
            if (actor->GetGraphVariableBool(animationDriven, on) && on) return true;
            if (actor->GetGraphVariableBool(speedSynced, on) && on) return true;
            return actor->GetGraphVariableBool(isSynced, on) && on;
        }

        void OnFrame(Frame::Next next, void* controller, float deltaTime, hooks::MovementFrame* out)
        {
            next(controller, deltaTime, out);
            RE::Actor* player = out ? PlayerOf(controller) : nullptr;
            if (!player) return;
            Ramp&       r      = g_ramp;
            const float target = out->speed;

            /// @note The graph's TargetSpeed (MiddleHighProcessData +0x2ac) is written only on the NPC path-following
            ///       branch, so for the player it carries the unramped speed from here.
            auto* process = player->GetActorRuntimeData().currentProcess;
            auto* mh      = process ? process->middleHigh : nullptr;
            if (mh) {
                bool expected = false;
                if (g_loggedTargetSpeed.compare_exchange_strong(expected, true))
                    LOG_INFO("speed ramp: player TargetSpeed was {:.1f} before CB's first write (speed {:.1f})", mh->unk2AC, target);
                mh->unk2AC = target;
            }

            if (!r.init || deltaTime <= 0.0f || GraphDrivesSpeed(player) || target <= r.speed + kRampThreshold) {
                if (r.ramping && LogBudget())
                    LOG_INFO("speed ramp: {:.1f} -> {:.1f} ended at {:.1f} after {:.3f} s (target now {:.1f})", r.from, r.speed, r.speed,
                             r.rampTime, target);
                r = Ramp{ true, false, target, target, 0.0f };
            } else {
                if (!r.ramping) {
                    r.ramping  = true;
                    r.from     = r.speed;
                    r.rampTime = 0.0f;
                    if (LogBudget()) LOG_INFO("speed ramp: {:.1f} -> {:.1f} (sprinting {})", r.speed, target, player->AsActorState()->IsSprinting());
                }
                r.speed = (std::min)(target, r.speed + target / RampTime() * deltaTime);
                r.rampTime += deltaTime;
                if (r.speed >= target) {
                    if (LogBudget()) LOG_INFO("speed ramp: reached {:.1f} after {:.3f} s", target, r.rampTime);
                    r.ramping = false;
                }
                out->speed = r.speed;
            }
            if (ch::hud::On()) ch::hud::Publish(ch::hud::Ramp{ out->speed, target });
        }

        [[maybe_unused]] const bool s_subscribed =
            core::debug::FlagOn("char_speed_ramp") && Frame::Subscribe(&OnFrame, hooks::Priority::Normal, "CharSpeedRamp");

    }  // namespace

}  // namespace CB::feature
