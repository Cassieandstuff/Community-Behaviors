#include "PCH.h"

#include "CharDebugHud.h"
#include "CharFacing.h"
#include "CharGround.h"
#include "CharMovementModel.h"

#include <platform/external/TrueDirectionalMovement/TrueDirectionalMovementAPI.h>
#include <platform/game/Movement.h>
#include <platform/hooks/Override.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <format>
#include <mutex>
#include <string>

/// @file
/// @brief The player's frame driver (char_movement_model): each frame the engine computes its frame from forward-only
///        input, so its speed is the gait's forward speed with every multiplier; CB then steps the movement model
///        (CharMovementModel) with the stick, that speed as the cap, the slope and MOVT's direction ratios, hands the
///        engine the model's velocity, and TDM the model's facing.
/// @note Without TDM the feature stays off (logged once). Yaw control is requested with multiplier 0, so TDM applies
///       CB's yaw at once and the turning dynamics are the model's spring, not TDM's rotation speed. Needs TDM's API V3.
/// @warning TDM answers BadThread off its own thread: every yaw call goes through OnTdmThread.

namespace CB::feature::controller {

    namespace {

        namespace hooks = platform::hooks;
        namespace game  = platform::game;
        using Frame     = hooks::Override<&game::MovementControllerNPC_ComputeFrameMovement>;

        /// @brief Below this stick length the player is not asking for a direction.
        constexpr float kStickDeadzone = 0.1f;
        constexpr float kTwoPi         = 6.2831853f;

        /// @note V3 is the newest interface CB needs (V2's yaw control, V3's mode and unaltered input), and the one to
        ///       ask for: TDM refuses a version newer than its own (v2.2.6 answers V5 with "wrong interface version").
        TDM_API::IVTDM3* Api()
        {
            static TDM_API::IVTDM3* const api = [] {
                auto* p = static_cast<TDM_API::IVTDM3*>(TDM_API::RequestPluginAPI(TDM_API::InterfaceVersion::V3));
                if (p) LOG_INFO("movement: True Directional Movement API V3 acquired (TDM thread {})", p->GetTDMThreadId());
                else   LOG_WARN("movement: True Directional Movement (V3 API) not found - CB's movement model stays off");
                return p;
            }();
            return api;
        }

        RE::Actor* PlayerOf(RE::MovementControllerNPC* controller)
        {
            auto* player = RE::PlayerCharacter::GetSingleton();
            if (!player) return nullptr;
            const auto& mc = player->GetActorRuntimeData().movementController;
            return mc && static_cast<void*>(mc.get()) == controller ? player : nullptr;
        }

        bool Holding(TDM_API::DirectionalMovementMode mode)
        {
            return mode == TDM_API::DirectionalMovementMode::kDirectional || mode == TDM_API::DirectionalMovementMode::kTargetLock;
        }

        /// @note The player's frame movement runs on one thread at a time; `g_haveControl` is written only on TDM's
        ///       thread (OnTdmThread) and read by the frame to skip work while there is nothing to hold or release.
        bool              g_init = false;
        move::State       g_state;
        std::atomic<bool> g_haveControl{ false };
        std::atomic<bool> g_loggedThread{ false };

        /// @brief The smoothed slope factor (ch::SlopeSpeedScale) the cap carries.
        struct Slope {
            bool  init  = false;
            float scale = 1.0f;
            float rate  = 0.0f;
        };
        Slope g_slope;

        /// @brief The engine's own frame speed last frame (before CB's model) and what CB handed on.
        struct EngineSpeed {
            bool  have    = false;
            float engine  = 0.0f;
            float written = 0.0f;
        };
        EngineSpeed       g_engineSpeed;
        std::atomic<int>  g_restoreLogs{ 0 }, g_keepLogs{ 0 };
        std::atomic<bool> g_loggedTargetSpeed{ false };

        /// @brief What CurrentSpeed hands out, stamped with the frame that wrote it.
        struct Published {
            std::mutex lock;
            FrameSpeed speed;
            double     at = -1.0e9;
        };
        Published        g_published;
        constexpr double kStale = 0.5;

        double Now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

        /// @brief Runs `fn` on TDM's thread: now when this is it, else as an SKSE task (the main thread).
        template <class Fn>
        void OnTdmThread(TDM_API::IVTDM3* api, Fn fn)
        {
            if (::GetCurrentThreadId() == api->GetTDMThreadId()) return fn();
            bool expected = false;
            if (g_loggedThread.compare_exchange_strong(expected, true))
                LOG_INFO("movement: frame runs on thread {}, TDM on {} - yaw calls are queued as SKSE tasks", ::GetCurrentThreadId(),
                         api->GetTDMThreadId());
            SKSE::GetTaskInterface()->AddTask(fn);
        }

        void Control(TDM_API::IVTDM3* api, bool want, float yaw)
        {
            OnTdmThread(api, [api, want, yaw] {
                const auto handle = SKSE::GetPluginHandle();
                if (want && !g_haveControl) {
                    const auto r = api->RequestYawControl(handle, 0.0f);
                    g_haveControl = r == TDM_API::APIResult::OK || r == TDM_API::APIResult::AlreadyGiven;
                    LOG_INFO("movement: yaw control {} (TDM result {})", g_haveControl ? "taken" : "refused", static_cast<int>(r));
                } else if (!want && g_haveControl) {
                    api->ReleaseYawControl(handle);
                    g_haveControl = false;
                    LOG_INFO("movement: yaw control released to TDM");
                }
                if (want && g_haveControl) {
                    float a = std::fmod(yaw, kTwoPi);
                    if (a < 0.0f) a += kTwoPi;
                    api->SetPlayerYaw(handle, a);
                }
            });
        }

        /// @brief While CB holds the yaw, the move input is CB's to rewrite: forward, at the stick's length.
        /// @note Why: the engine then computes a native forward frame (forward direction, forward MOVT speed with every
        ///       multiplier), which is the model's cap; the direction is the model's, written after. TDM's ProcessInput
        ///       returns before its own rewrite while another plugin holds the yaw, so the raw stick would reach the game.
        ///       The player-controls agents read PlayerControls::data.moveInputVec while computing this frame, so it is
        ///       rewritten before `next`. The stick comes from TDM's GetActualMovementInput, never from moveInputVec.
        void RewriteInput(TDM_API::IVTDM3* api)
        {
            const RE::NiPoint2 stick = api->GetActualMovementInput();
            const float        len   = std::sqrt(stick.x * stick.x + stick.y * stick.y);
            if (len < kStickDeadzone) return;
            auto* controls = RE::PlayerControls::GetSingleton();
            if (!controls) return;
            controls->data.moveInputVec = RE::NiPoint2(0.0f, (std::min)(1.0f, len));
        }

        /// @brief The graph flag under which the graph drives the speed itself (animation-driven, speed-synced or
        ///        synced), the frame then left alone; empty when none is set.
        const char* GraphDrivesSpeed(RE::Actor* actor)
        {
            static const RE::BSFixedString animationDriven("bAnimationDriven");
            static const RE::BSFixedString speedSynced("bSpeedSynced");
            static const RE::BSFixedString isSynced("bIsSynced");
            bool on = false;
            if (actor->GetGraphVariableBool(animationDriven, on) && on) return "bAnimationDriven";
            if (actor->GetGraphVariableBool(speedSynced, on) && on) return "bSpeedSynced";
            if (actor->GetGraphVariableBool(isSynced, on) && on) return "bIsSynced";
            return "";
        }

        std::atomic<int> g_diagLogs{ 0 };
        bool             DiagBudget() { return g_diagLogs.fetch_add(1, std::memory_order_relaxed) < 600; }

        Gait GaitOf(RE::Actor* actor)
        {
            const auto* state = actor->AsActorState();
            return state->IsSprinting() ? Gait::Sprint : state->IsRunning() ? Gait::Run : state->IsWalking() ? Gait::Walk : Gait::None;
        }

        /// @brief MOVT's left, right and back speeds as fractions of its forward speed, for the gait's column (walk, or run
        ///        for run and sprint).
        void DirectionRatios(RE::Actor* actor, Gait gait, move::Caps& caps)
        {
            auto* process = actor->GetActorRuntimeData().currentProcess;
            auto* high    = process ? process->high : nullptr;
            if (!high) return;
            using Dir         = RE::Movement::SPEED_DIRECTIONS;
            const auto& sp    = high->currentMovementType.defaultData.speeds;
            const int   col   = gait == Gait::Walk ? RE::Movement::MaxSpeeds::kWalk : RE::Movement::MaxSpeeds::kRun;
            /// @note A direction with no speed in the gait's column falls back to the run column's ratio: NPCSprinting has
            ///       no left or right speed at all, which capped a sprinting strafe at zero.
            const auto ratio = [&](Dir::SPEED_DIRECTION d, float& out) {
                for (const int c : { col, static_cast<int>(RE::Movement::MaxSpeeds::kRun) }) {
                    if (sp[Dir::kForward][c] > 0.0f && sp[d][c] > 0.0f) {
                        out = sp[d][c] / sp[Dir::kForward][c];
                        return;
                    }
                }
            };
            ratio(Dir::kLeft, caps.left);
            ratio(Dir::kRight, caps.right);
            ratio(Dir::kBack, caps.back);
        }

        /// @brief Once per gait: the MOVT speeds beside GASP's absolute sprint thresholds, which were converted from
        ///        centimetres and not rescaled to Skyrim's speeds.
        void LogSpeeds(RE::Actor* actor, Gait gait, float full)
        {
            static std::array<std::atomic<bool>, 4> logged{};
            auto& once = logged[static_cast<std::size_t>(gait) & 3];
            if (gait == Gait::None || full <= 0.0f || once.exchange(true)) return;
            auto* process = actor->GetActorRuntimeData().currentProcess;
            auto* high    = process ? process->high : nullptr;
            if (!high) return;
            using Dir      = RE::Movement::SPEED_DIRECTIONS;
            const auto& sp = high->currentMovementType.defaultData.speeds;
            static constexpr const char* kNames[4] = { "none", "walk", "run", "sprint" };
            LOG_INFO("movement: {} - engine forward speed {:.0f}; MOVT '{}' walk F/L/R/B {:.0f}/{:.0f}/{:.0f}/{:.0f}, run {:.0f}/{:.0f}/{:.0f}/{:.0f}; "
                     "GASP's sprint ramps: acceleration over {:.0f}->{:.0f}, friction over 0->{:.0f}",
                     kNames[static_cast<int>(gait) & 3], full, high->currentMovementType.typeName.c_str(), sp[Dir::kForward][0], sp[Dir::kLeft][0],
                     sp[Dir::kRight][0], sp[Dir::kBack][0], sp[Dir::kForward][1], sp[Dir::kLeft][1], sp[Dir::kRight][1], sp[Dir::kBack][1],
                     move::kSprintAccelFrom, move::kSprintAccelTo, move::kSprintFrictionTo);
        }

        float CapNow(const move::Caps& caps, const move::Input& in, const move::State& s)
        {
            return move::MaxSpeed(caps, in.mode, std::hypot(s.vx, s.vy) > 1.0f ? std::atan2(s.vx, s.vy) : s.yaw, s.yaw);
        }

        bool Airborne(RE::Actor* actor)
        {
            auto* ctrl = actor->GetCharController();
            if (!ctrl) return false;
            const auto state = ctrl->context.currentState;
            return state == RE::hkpCharacterStateType::kInAir || state == RE::hkpCharacterStateType::kJumping;
        }

        /// @brief Who steers the player this frame while CB would hold them.
        enum class Steer : std::uint8_t {
            Player,     ///< the player's own input: CB's model drives
            Dialogue,   ///< the dialogue menu is open: CB holds still and faces the speaker
            Vanilla,    ///< AI-driven (scenes, packages) or movement controls disabled: vanilla's, untouched
        };

        /// @param speaker set to the dialogue speaker's position when the result is Dialogue and one resolves
        Steer WhoSteers(RE::MovementControllerNPC* controller, bool& haveSpeaker, RE::NiPoint3& speaker)
        {
            haveSpeaker = false;
            if (auto* topics = RE::MenuTopicManager::GetSingleton(); topics && topics->menuOpen) {
                if (const auto ref = topics->speaker.get()) {
                    speaker     = ref->GetPosition();
                    haveSpeaker = true;
                }
                return Steer::Dialogue;
            }
            if (controller->GetAIDriven()) return Steer::Vanilla;
            if (auto* controls = RE::ControlMap::GetSingleton(); controls && !controls->IsMovementControlsEnabled()) return Steer::Vanilla;
            return Steer::Player;
        }

        void OnFrame(Frame::Next next, RE::MovementControllerNPC* controller, float deltaTime, game::MovementFrame* out)
        {
            RE::Actor* player  = out ? PlayerOf(controller) : nullptr;
            auto*      api     = player ? Api() : nullptr;
            auto*      process = player ? player->GetActorRuntimeData().currentProcess : nullptr;
            auto*      mh      = process ? process->middleHigh : nullptr;

            /// @note The engine's own speed state is MiddleHighProcessData::currentMovementSpeed (+0x2a8): the frame
            ///       movement starts from it, PlayerControls (0x14079d630) reads it as the current speed and writes the
            ///       stick's target into it, and Actor_ApplyFrameMovement writes the frame's speed (ours, after this
            ///       hook) back into it for the graph's Speed. Left there, CB's speed would become the engine's next
            ///       starting point (a per-frame factor once compounded 435 -> 12868 game units/s in 1.4 s), so before the
            ///       engine computes, the state is put back to the engine's own last value, unless the engine wrote a new
            ///       one since (the stick moved).
            /// @invariant The engine never sees CB's speed as its own.
            EngineSpeed& es = g_engineSpeed;
            if (mh && es.have) {
                const float seen = mh->currentMovementSpeed;
                if (std::fabs(seen - es.written) <= 1.0e-3f * (std::max)(1.0f, std::fabs(es.written))) {
                    if (es.written != es.engine && g_restoreLogs.fetch_add(1, std::memory_order_relaxed) < 5)
                        LOG_INFO("movement: engine speed state put back {:.1f} -> {:.1f}", seen, es.engine);
                    mh->currentMovementSpeed = es.engine;
                } else if (g_keepLogs.fetch_add(1, std::memory_order_relaxed) < 5) {
                    LOG_INFO("movement: engine speed state {:.1f} is not CB's {:.1f} - the engine set it, kept", seen, es.written);
                }
            }

            const auto mode = api ? api->GetDirectionalMovementMode() : TDM_API::DirectionalMovementMode::kDisabled;
            const bool hold = api && Holding(mode) && RE::PlayerCamera::GetSingleton() &&
                              RE::PlayerCamera::GetSingleton()->IsCurrentState(RE::CameraStates::kThirdPerson, RE::CameraStates::kVRThirdPerson);
            bool         haveSpeaker = false;
            RE::NiPoint3 speakerAt;
            const Steer  steer = hold ? WhoSteers(controller, haveSpeaker, speakerAt) : Steer::Player;
            /// @note Taking the player out of the player's hands: the engine's speed state still holds the speed of the
            ///       last frame CB drove, and with the stick ignored nothing brings it down, so the player slid on at it
            ///       (seen in dialogue). It is zeroed once, at the hand-over: from then on vanilla, the AI or the scene sets it.
            /// @warning Only the player's frames may touch it: every actor's frame runs this hook, on job threads in
            ///          parallel, and an NPC's "player" verdict would re-arm the hand-over every frame.
            static Steer lastSteer = Steer::Player;
            if (player && steer != lastSteer) {
                if (lastSteer == Steer::Player && mh) mh->currentMovementSpeed = 0.0f;
                if (steer != Steer::Player) g_engineSpeed = EngineSpeed{};
                if (DiagBudget())
                    LOG_INFO("movement: {} steers the player", steer == Steer::Player ? "the player" : steer == Steer::Dialogue ? "dialogue" : "vanilla");
                lastSteer = steer;
            }
            float      stickHeading = 0.0f;
            const bool haveStick    = steer == Steer::Player && api && StickHeading(stickHeading);
            if (api && g_haveControl && Holding(mode) && steer == Steer::Player) RewriteInput(api);
            next(controller, deltaTime, out);
            if (!api) return;

            const float heading = player->GetHeading(false);
            const float full    = out->speed;
            const char* graphFlag   = hold ? GraphDrivesSpeed(player) : "";
            const bool  graphDrives = graphFlag[0] != 0;
            /// @note Diagnostic: every hand-over to the graph and back, with the speeds either side.
            static const char* lastFlag = "";
            if (graphFlag != lastFlag) {
                if (DiagBudget())
                    LOG_INFO("movement: graph {} the speed ({}) - model {:.0f}, engine frame {:.0f}", *graphFlag ? "takes" : "returns",
                             *graphFlag ? graphFlag : lastFlag, std::hypot(g_state.vx, g_state.vy), out->speed);
                lastFlag = graphFlag;
            }
            move::State& s = g_state;
            const bool   vanilla = steer == Steer::Vanilla;
            if (!hold || !g_init || graphDrives || vanilla || deltaTime <= 0.0f) {
                /// @note Off, first frame, the graph moving the actor, or vanilla steering: the model restarts from the
                ///       engine's own frame.
                const float dir = heading + out->angles[2];
                s       = move::State{ out->speed * std::sin(dir), out->speed * std::cos(dir), heading, 0.0f };
                g_init  = hold && !vanilla;
                es      = EngineSpeed{};
                if (hold || g_haveControl) Control(api, hold && !vanilla, heading);
                if (!hold || graphDrives || vanilla) return;
            }

            if (steer == Steer::Dialogue) {
                /// @note In dialogue the player stands: the model's velocity is dropped, and the facing spring turns them
                ///       toward the speaker (when one resolves), with the model's halflife.
                s.vx = s.vy = 0.0f;
                s.yaw       = heading + WrapPi(s.yaw - heading);
                if (haveSpeaker) {
                    const RE::NiPoint3 at   = player->GetPosition();
                    const float        dx   = speakerAt.x - at.x, dy = speakerAt.y - at.y;
                    if (dx * dx + dy * dy > 1.0f) {
                        const float goal = s.yaw + WrapPi(std::atan2(dx, dy) - s.yaw);
                        SpringDamperExact(s.yaw, s.yawRate, goal, move::kFacingHalflife, deltaTime);
                    }
                }
                out->speed     = 0.0f;
                out->angles[2] = 0.0f;
                Control(api, true, s.yaw);
                return;
            }

            /// @note The graph's TargetSpeed (MiddleHighProcessData +0x2ac) is written only on the NPC path-following
            ///       branch, so for the player it carries the forward speed asked for.
            if (mh) {
                bool expected = false;
                if (g_loggedTargetSpeed.compare_exchange_strong(expected, true))
                    LOG_INFO("movement: player TargetSpeed was {:.1f} before CB's first write (speed {:.1f})", mh->unk2AC, full);
                mh->unk2AC = full;
            }

            /// @note Keep the spring on the heading the game actually has (TDM's own turns, a teleport, a sync scene).
            s.yaw = heading + WrapPi(s.yaw - heading);

            move::Input in;
            in.mode      = mode == TDM_API::DirectionalMovementMode::kTargetLock ? move::Mode::Strafe : move::Mode::Orient;
            in.gait      = GaitOf(player);
            in.airborne  = Airborne(player);
            in.haveStick = haveStick;
            if (haveStick) in.stickX = std::sin(stickHeading), in.stickY = std::cos(stickHeading);
            if (in.mode == move::Mode::Strafe) {
                if (const auto target = api->GetCurrentTarget().get()) {
                    const RE::NiPoint3 at = target->GetPosition();
                    in.haveTarget = true, in.targetX = at.x, in.targetY = at.y;
                }
            }

            /// @note The slope factor follows the ground under the player along the velocity, smoothed so its drops
            ///       ease as a crest passes; it scales the cap, so acceleration and braking ease toward it.
            float            slopeGoal = 1.0f;
            FloorVerdict     ground;
            const float      speed0 = std::hypot(s.vx, s.vy);
            if (LatestVerdict(ground)) {
                const float dir = speed0 > 1.0f ? std::atan2(s.vx, s.vy) : (haveStick ? stickHeading : heading);
                slopeGoal       = SlopeSpeedScale(ground, std::sin(dir), std::cos(dir));
            }
            Slope& sl = g_slope;
            if (!sl.init) sl = Slope{ true, slopeGoal, 0.0f };
            else SpringDamperExact(sl.scale, sl.rate, slopeGoal, kSlopeSpeedHalflife, deltaTime);

            move::Caps caps;
            caps.forward = full * sl.scale;
            DirectionRatios(player, in.gait, caps);
            LogSpeeds(player, in.gait, full);

            const RE::NiPoint3 pos = player->GetPosition();
            move::Step(s, in, caps, pos.x, pos.y, deltaTime);

            const float speed = std::hypot(s.vx, s.vy);
            out->speed        = speed;
            out->angles[2]    = speed > 0.5f ? WrapPi(std::atan2(s.vx, s.vy) - heading) : 0.0f;
            g_engineSpeed     = EngineSpeed{ true, full, speed };
            Control(api, true, s.yaw);

            move::Publish(move::Snapshot{ s, in, caps, pos.x, pos.y });
            /// @note Diagnostic: twice a second while moving, the model's inputs and output.
            static auto lastDiag = std::chrono::steady_clock::now();
            if (const auto now = std::chrono::steady_clock::now(); speed > 1.0f && now - lastDiag > std::chrono::milliseconds(500)) {
                lastDiag = now;
                static constexpr const char* kGaits[4] = { "none", "walk", "run", "sprint" };
                constexpr float kRad = 57.29578f;
                if (DiagBudget())
                    LOG_INFO("movement: {} {} - stick {} deg, velocity {:.0f} at {:.0f} deg, cap {:.0f} (forward {:.0f} = engine {:.0f} x slope "
                             "{:.2f}; L/R/B {:.2f}/{:.2f}/{:.2f}){}",
                             in.mode == move::Mode::Strafe ? "strafe" : "orient", kGaits[static_cast<int>(in.gait) & 3],
                             haveStick ? std::format("{:.0f}", WrapPi(stickHeading - s.yaw) * kRad) : std::string("none"), speed,
                             WrapPi(std::atan2(s.vx, s.vy) - s.yaw) * kRad, CapNow(caps, in, s), caps.forward, full, sl.scale, caps.left, caps.right,
                             caps.back, in.airborne ? ", airborne" : "");
            }
            {
                std::scoped_lock lock(g_published.lock);
                g_published.speed = FrameSpeed{ full, speed, sl.scale, in.gait };
                g_published.at    = Now();
            }
            if (hud::On()) {
                hud::Movement h;
                h.strafe    = in.mode == move::Mode::Strafe;
                h.speed     = speed;
                h.full      = full;
                h.slope     = sl.scale;
                h.cap       = move::MaxSpeed(caps, in.mode, speed > 1.0f ? std::atan2(s.vx, s.vy) : s.yaw, s.yaw);
                h.yaw       = s.yaw;
                h.haveStick = haveStick;
                h.stickYaw  = stickHeading;
                hud::Publish(h);
            }
        }

        [[maybe_unused]] const bool s_subscribed = Frame::Subscribe(&OnFrame, hooks::Priority::Late, "CharFacing", "char_movement_model");

    }  // namespace

    bool CurrentSpeed(FrameSpeed& out)
    {
        std::scoped_lock lock(g_published.lock);
        if (Now() - g_published.at > kStale) return false;
        out = g_published.speed;
        return true;
    }

    bool StickHeading(float& heading)
    {
        auto* api    = Api();
        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* camera = RE::PlayerCamera::GetSingleton();
        if (!api || !player || !camera) return false;
        if (!Holding(api->GetDirectionalMovementMode())) return false;
        if (!camera->IsCurrentState(RE::CameraStates::kThirdPerson, RE::CameraStates::kVRThirdPerson)) return false;
        const RE::NiPoint2 stick = api->GetActualMovementInput();
        if (stick.x * stick.x + stick.y * stick.y < kStickDeadzone * kStickDeadzone) return false;
        const auto* third     = static_cast<const RE::ThirdPersonState*>(camera->currentState.get());
        const float cameraYaw = player->GetHeading(false) + (third->freeRotationEnabled ? third->freeRotation.x : 0.0f);
        heading               = cameraYaw + std::atan2(stick.x, stick.y);
        return true;
    }

}  // namespace CB::feature::controller
