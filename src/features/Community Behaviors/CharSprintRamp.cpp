#include "PCH.h"

#include "CharGround.h"

#include <core/debug/Settings.h>
#include <core/hooks/hookslib.h>

#include <algorithm>
#include <cstdint>

/// @file
/// @brief Sprint acceleration for the player: ramps the speed the movement controller produces each frame.
/// @note Why here and not in the character controller: vanilla's player path has no acceleration (the speed jumps
///       to its target; PlayerControls only limits a drop of more than 500 game units/s in one call), and the same
///       speed feeds the graph's `Speed` (what BSSpeedSamplerModifier fits the locomotion blend to) and the frame
///       movement physics integrates. Ramping only the physics would play full sprint over a body still speeding
///       up; ramping this one number keeps animation and physics together.

namespace CB::feature {

    namespace {

        namespace hooks = core::hooks;
        namespace ch    = CB::feature::character;
        using Frame     = hooks::MovementControllerFrame;

        struct Tunables {
            float acceleration = 300.0f;   ///< game units/s^2 while ramping
            float baseFraction = 0.6f;     ///< a ramp from rest starts at this fraction of sprint speed
        };

        const Tunables& Tune()
        {
            static const Tunables t = [] {
                Tunables r;
                r.acceleration = (std::max)(1.0f, ch::SettingFloat("fSprintAcceleration", r.acceleration));
                r.baseFraction = std::clamp(ch::SettingFloat("fSprintRampBase", r.baseFraction), 0.0f, 1.0f);
                LOG_INFO("sprint ramp: acceleration {:.0f} game units/s^2, base {:.2f} of sprint speed", r.acceleration, r.baseFraction);
                return r;
            }();
            return t;
        }

        /// @brief The player's ramp between frames.
        /// @note Only the player's controller reaches it, from one thread at a time, so it is plain state.
        struct Ramp {
            bool  init       = false;
            bool  sprinting  = false;
            float speed      = 0.0f;   ///< the speed handed on last frame (game units/s)
            float runTarget  = 0.0f;   ///< the target speed on the last frame before the sprint
            float rampTime   = 0.0f;
            bool  loggedTargetSpeed = false;
        };
        Ramp g_ramp;

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
                if (!r.loggedTargetSpeed) {
                    r.loggedTargetSpeed = true;
                    LOG_INFO("sprint ramp: player TargetSpeed was {:.1f} before CB's first write (speed {:.1f})", mh->unk2AC, target);
                }
                mh->unk2AC = target;
            }

            const bool sprinting = player->AsActorState()->IsSprinting();
            if (!r.init || deltaTime <= 0.0f || GraphDrivesSpeed(player)) {
                r = Ramp{ true, sprinting, target, target, 0.0f, r.loggedTargetSpeed };
                return;
            }
            if (sprinting != r.sprinting) {
                LOG_INFO("sprint ramp: sprint {} (target {:.1f}, speed {:.1f}, run target {:.1f})", sprinting ? "on" : "off", target, r.speed,
                         r.runTarget);
                r.sprinting = sprinting;
                r.rampTime  = 0.0f;
            }
            if (!sprinting || target <= r.speed) {
                if (!sprinting) r.runTarget = target;
                r.speed = target;
                return;
            }
            const Tunables& t    = Tune();
            const float     base = (std::min)(target, (std::max)(r.runTarget, target * t.baseFraction));
            r.speed              = (std::min)(target, (std::max)(base, r.speed + t.acceleration * deltaTime));
            r.rampTime += deltaTime;
            if (r.speed >= target) LOG_INFO("sprint ramp: reached {:.1f} after {:.3f} s", target, r.rampTime);
            out->speed = r.speed;
        }

        [[maybe_unused]] const bool s_subscribed =
            core::debug::FlagOn("char_sprint_ramp") && Frame::Subscribe(&OnFrame, hooks::Priority::Normal, "CharSprintRamp");

    }  // namespace

}  // namespace CB::feature
