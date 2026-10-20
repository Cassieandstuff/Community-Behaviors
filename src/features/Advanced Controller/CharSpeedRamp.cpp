#include "PCH.h"

#include "CharDebugHud.h"
#include "CharFacing.h"
#include "CharGround.h"

#include <core/debug/Settings.h>
#include <platform/game/Movement.h>
#include <platform/hooks/Override.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <mutex>

/// @file
/// @brief Speed acceleration for the player: ramps the speed the movement controller produces each frame, and slows
///        it while the facing turns toward the stick (TurnSpeedScale, under CharFacing), so a hard turn rebuilds speed,
///        and by the slope along the run (SlopeSpeedScale): faster downhill, slower uphill.
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
        namespace game  = CB::platform::game;
        using Frame     = hooks::Override<&game::MovementControllerNPC_ComputeFrameMovement>;

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

        /// @brief The smoothed slope factor (ch::SlopeSpeedScale), kept apart from the ramp, which resets on every drop.
        struct Slope {
            bool  init  = false;
            float scale = 1.0f;
            float rate  = 0.0f;
        };
        Slope g_slope;

        /// @brief The engine's own frame speed last frame (before CB's ramp, turn and slope) and what CB handed on.
        struct EngineSpeed {
            bool  have    = false;
            float engine  = 0.0f;
            float written = 0.0f;
        };
        EngineSpeed      g_engineSpeed;
        std::atomic<int> g_restoreLogs{ 0 }, g_keepLogs{ 0 };
        std::atomic<bool> g_loggedTargetSpeed{ false };
        std::atomic<int>  g_logs{ 0 };

        bool LogBudget() { return g_logs.fetch_add(1, std::memory_order_relaxed) < 2000; }

        RE::Actor* PlayerOf(RE::MovementControllerNPC* controller)
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

        /// @brief What CurrentSpeed hands out, stamped with the frame that wrote it.
        struct Published {
            std::mutex     lock;
            ch::FrameSpeed speed;
            double         at = -1.0e9;
        };
        Published        g_published;
        constexpr double kStale = 0.5;

        double Now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

        /// @note Priority: CharFacing subscribes Late (inside this one), so its facing for this frame is published by
        ///       the time `next` returns here.
        void OnFrame(Frame::Next next, RE::MovementControllerNPC* controller, float deltaTime, game::MovementFrame* out)
        {
            RE::Actor* player  = out ? PlayerOf(controller) : nullptr;
            auto*      process = player ? player->GetActorRuntimeData().currentProcess : nullptr;
            auto*      mh      = process ? process->middleHigh : nullptr;

            /// @note The engine's own speed state is MiddleHighProcessData::currentMovementSpeed (+0x2a8): the frame
            ///       movement starts from it, PlayerControls (0x14079d630) reads it as the current speed and writes the
            ///       stick's target into it, and Actor_ApplyFrameMovement writes the frame's speed (ours, after this
            ///       hook) back into it for the graph's Speed. So whatever CB hands on became the next frame's starting
            ///       point, and a per-frame factor compounded: one downhill run went 435 -> 12868 game units/s in 1.4 s
            ///       and stayed there, an uphill one decayed to a crawl. Before the engine computes, the state is put back
            ///       to the engine's own last value, unless the engine wrote a new one since (the stick moved).
            /// @invariant CB post-processes the frame speed; the engine never sees CB's value as its own.
            EngineSpeed& es = g_engineSpeed;
            if (mh && es.have) {
                const float seen = mh->currentMovementSpeed;
                if (std::fabs(seen - es.written) <= 1.0e-3f * (std::max)(1.0f, std::fabs(es.written))) {
                    if (es.written != es.engine && g_restoreLogs.fetch_add(1, std::memory_order_relaxed) < 5)
                        LOG_INFO("speed ramp: engine speed state put back {:.1f} -> {:.1f}", seen, es.engine);
                    mh->currentMovementSpeed = es.engine;
                } else if (g_keepLogs.fetch_add(1, std::memory_order_relaxed) < 5) {
                    LOG_INFO("speed ramp: engine speed state {:.1f} is not CB's {:.1f} - the engine set it, kept", seen, es.written);
                }
            }
            next(controller, deltaTime, out);
            if (!player) return;
            Ramp&       r    = g_ramp;
            const float full = out->speed;

            /// @note The graph's TargetSpeed (MiddleHighProcessData +0x2ac) is written only on the NPC path-following
            ///       branch, so for the player it carries the speed asked for from here: unramped and undamped.
            if (mh) {
                bool expected = false;
                if (g_loggedTargetSpeed.compare_exchange_strong(expected, true))
                    LOG_INFO("speed ramp: player TargetSpeed was {:.1f} before CB's first write (speed {:.1f})", mh->unk2AC, full);
                mh->unk2AC = full;
            }

            const bool      graphDrives = GraphDrivesSpeed(player);
            float           turnScale   = 1.0f;
            ch::FacingState facing;
            if (!graphDrives && ch::CurrentFacing(facing) && facing.haveGoal) turnScale = ch::TurnSpeedScale(facing.goal - facing.yaw);

            /// @note The slope factor follows the ground under the player along the direction the frame moves them: the
            ///       heading turned by the frame's own movement direction (angles[2], 0 = forward; always 0 under True
            ///       Directional Movement's forward-only input). Smoothed, so its drops ease as a crest passes.
            /// @warning angles[2]'s sign (clockwise like the heading) is assumed, unverified for a sideways request.
            float           slopeGoal = 1.0f;
            ch::FloorVerdict ground;
            if (!graphDrives && ch::LatestVerdict(ground)) {
                const float dir = player->GetHeading(false) + out->angles[2];
                slopeGoal       = ch::SlopeSpeedScale(ground, std::sin(dir), std::cos(dir));
            }
            Slope& sl = g_slope;
            if (!sl.init || deltaTime <= 0.0f) sl = Slope{ true, slopeGoal, 0.0f };
            else ch::SpringDamperExact(sl.scale, sl.rate, slopeGoal, ch::kSlopeSpeedHalflife, deltaTime);
            const float target = graphDrives ? full : full * turnScale * sl.scale;

            /// @note The rise rate is the asked-for speed over fRampTime, not the damped target's: a body coming out of
            ///       a turn rebuilds its speed at the same acceleration as one starting from rest.
            if (!r.init || deltaTime <= 0.0f || graphDrives || target <= r.speed + kRampThreshold) {
                if (r.ramping && LogBudget())
                    LOG_INFO("speed ramp: {:.1f} -> {:.1f} ended at {:.1f} after {:.3f} s (target now {:.1f}, turn {:.2f}, slope {:.2f})", r.from, r.speed, r.speed,
                             r.rampTime, target, turnScale, sl.scale);
                r = Ramp{ true, false, target, target, 0.0f };
            } else {
                if (!r.ramping) {
                    r.ramping  = true;
                    r.from     = r.speed;
                    r.rampTime = 0.0f;
                    if (LogBudget())
                        LOG_INFO("speed ramp: {:.1f} -> {:.1f} (turn {:.2f}, slope {:.2f}, sprinting {})", r.speed, target, turnScale, sl.scale,
                                 player->AsActorState()->IsSprinting());
                }
                r.speed = (std::min)(target, r.speed + full / RampTime() * deltaTime);
                r.rampTime += deltaTime;
                if (r.speed >= target) {
                    if (LogBudget()) LOG_INFO("speed ramp: reached {:.1f} after {:.3f} s", target, r.rampTime);
                    r.ramping = false;
                }
            }
            out->speed    = r.speed;
            g_engineSpeed = EngineSpeed{ true, full, r.speed };
            {
                std::scoped_lock lock(g_published.lock);
                g_published.speed = ch::FrameSpeed{ full, r.speed, RampTime(), sl.scale };
                g_published.at    = Now();
            }
            if (ch::hud::On()) ch::hud::Publish(ch::hud::Ramp{ out->speed, full, turnScale, sl.scale });
        }

        [[maybe_unused]] const bool s_subscribed =
            Frame::Subscribe(&OnFrame, hooks::Priority::Normal, "CharSpeedRamp", "char_speed_ramp");

    }  // namespace

    bool controller::CurrentSpeed(FrameSpeed& out)
    {
        std::scoped_lock lock(g_published.lock);
        if (Now() - g_published.at > kStale) return false;
        out = g_published.speed;
        return true;
    }

}  // namespace CB::feature
