#include "PCH.h"

#include "CharDebugHud.h"
#include "CharFacing.h"
#include "CharGround.h"
#include "CharMovementModel.h"

#include <features/Camera/Camera.h>
#include <platform/game/Movement.h>
#include <platform/hooks/Override.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <format>
#include <limits>
#include <mutex>
#include <string>
#include <unordered_map>

/// @file
/// @brief The frame drivers. The player's (char_movement_model): each frame the engine computes its frame from
///        forward-only input, so its speed is the gait's forward speed with every multiplier; CB then steps the movement
///        model (CharMovementModel) with the stick, that speed as the cap, the slope and MOVT's direction ratios, and
///        hands the engine the model's velocity and facing. The ridden horse's (horse_movement_model, OnHorseFrame).
/// @note It holds the player only while CB's camera drives the third-person camera (camera::ControlYaw): the stick is
///       relative to the camera's yaw. Otherwise the frame stays the engine's.
/// @note The facing goes through the frame's turn rate, which Actor_ApplyFrameMovement applies to the heading before it
///       moves the actor (platform/game/Movement.h), so the direction is written relative to the new heading.

namespace CB::feature::controller {

    namespace {

        namespace hooks = platform::hooks;
        namespace game  = platform::game;
        using Frame     = hooks::Override<&game::MovementControllerNPC_ComputeFrameMovement>;

        /// @brief Below this stick length the player is not asking for a direction.
        constexpr float kStickDeadzone = 0.1f;
        /// @brief How far ahead of the player the camera-facing target sits: far enough that its bearing is the
        ///        camera's yaw from anywhere the player moves within a frame.
        constexpr float kFaceCameraReach = 100000.0f;

        RE::Actor* PlayerOf(RE::MovementControllerNPC* controller)
        {
            auto* player = RE::PlayerCharacter::GetSingleton();
            if (!player) return nullptr;
            const auto& mc = player->GetActorRuntimeData().movementController;
            return mc && static_cast<void*>(mc.get()) == controller ? player : nullptr;
        }

        /// @brief The raw stick this frame (PlayerControls moveInputVec before CB rewrites it), for StickHeading.
        /// @note Published, not re-read: the rewrite stands while the engine computes the frame, and readers on other
        ///       threads (the graph's predictor) must never see it as the stick.
        struct Stick {
            std::atomic<float>  x{ 0.0f }, y{ 0.0f };
            std::atomic<double> at{ -1.0e9 };
        };
        Stick g_stick;

        /// @brief The smoothed slope factor (ch::SlopeSpeedScale) the cap carries.
        struct Slope {
            bool  init  = false;
            float scale = 1.0f;
            float rate  = 0.0f;
        };

        /// @brief The engine's own frame speed and direction (angles[2]) last frame (before CB's model) and what CB
        ///        handed on.
        struct EngineSpeed {
            bool  have       = false;
            float engine     = 0.0f;
            float written    = 0.0f;
            float engineDir  = 0.0f;
            float writtenDir = 0.0f;
        };

        /// @brief One driven actor's frame state, carried from frame to frame.
        struct Driven {
            bool        init = false;   ///< the model has stepped since it last restarted from the engine's frame
            move::State state;
            Slope       slope;
            EngineSpeed engineSpeed;
        };
        /// @brief Every driven actor's state, by FormID (the player today; a ridden horse next).
        /// @note  Each actor's frame runs on one movement job at a time, so only that actor's frame touches its entry: the
        ///        lock guards the table, not the entries. Map nodes are stable, so an entry's reference outlives later
        ///        inserts. Never pruned: a handful of actors.
        struct DrivenTable {
            std::mutex                              lock;
            std::unordered_map<RE::FormID, Driven> byActor;

            Driven& Of(RE::FormID actor)
            {
                std::scoped_lock guard(lock);
                return byActor[actor];
            }
        };
        DrivenTable       g_driven;
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

        constexpr float kNoDirection = std::numeric_limits<float>::quiet_NaN();

        /// @brief Turns the frame toward `yaw`: a turn rate that lands the heading on it this frame, and the move
        ///        direction relative to that new heading.
        /// @param worldDir the direction to move in (world yaw), or kNoDirection to leave `angles[2]` as it is
        void Face(game::MovementFrame* out, float heading, float yaw, float worldDir, float dt)
        {
            out->turn[2] = dt > 0.0f ? WrapPi(yaw - heading) / dt : 0.0f;
            if (!std::isnan(worldDir)) out->angles[2] = WrapPi(worldDir - (dt > 0.0f ? yaw : heading));
        }

        /// @brief The world heading `stick` asks for, read relative to the camera's yaw (+y = away from the camera).
        bool StickHeadingOf(RE::NiPoint2 stick, float cameraYaw, float& heading)
        {
            if (stick.x * stick.x + stick.y * stick.y < kStickDeadzone * kStickDeadzone) return false;
            heading = cameraYaw + std::atan2(stick.x, stick.y);
            return true;
        }

        /// @brief While CB holds the player, the move input is CB's to rewrite: forward, at the stick's length.
        /// @note Why: the engine then computes a native forward frame (forward direction, forward MOVT speed with every
        ///       multiplier), which is the model's cap; the direction is the model's, written after. The player-controls
        ///       agents read PlayerControls::data.moveInputVec while computing this frame, so it is rewritten before
        ///       `next` and the raw stick put back after.
        /// @note PlayerControls rebuilds moveInputVec from zero on every input dispatch (0x14079a970), so outside the
        ///       frame it is the raw stick.
        void RewriteInput(RE::PlayerControls* controls, RE::NiPoint2 raw)
        {
            const float len = std::sqrt(raw.x * raw.x + raw.y * raw.y);
            if (len < kStickDeadzone) return;
            controls->data.moveInputVec = RE::NiPoint2(0.0f, (std::min)(1.0f, len));
        }

        /// @brief Whether the player is doing something aimed (an attack or a bow, blocking, casting): then the body
        ///        faces the camera and strafes.
        bool Aiming(RE::Actor* actor)
        {
            return actor->AsActorState()->GetAttackState() != RE::ATTACK_STATE_ENUM::kNone || actor->IsBlocking() || actor->WhoIsCasting() != 0;
        }

        /// @brief The graph flag under which the graph drives the speed itself (ch::GraphMovesActor's, or speed-synced),
        ///        the frame then left alone; empty when none is set.
        const char* GraphDrivesSpeed(RE::Actor* actor)
        {
            static const RE::BSFixedString speedSynced("bSpeedSynced");
            if (const char* moves = GraphMovesActor(actor); *moves) return moves;
            bool on = false;
            if (actor->GetGraphVariableBool(speedSynced, on) && on) return "bSpeedSynced";
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
                     move::kPlayer.sprintAccelFrom, move::kPlayer.sprintAccelTo, move::kPlayer.sprintFrictionTo);
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
            RE::Actor* player   = out ? PlayerOf(controller) : nullptr;
            auto*      controls = player ? RE::PlayerControls::GetSingleton() : nullptr;
            auto*      process  = player ? player->GetActorRuntimeData().currentProcess : nullptr;
            auto*      mh       = process ? process->middleHigh : nullptr;
            Driven*    driven   = player ? &g_driven.Of(player->GetFormID()) : nullptr;

            /// @note The engine's own speed state is MiddleHighProcessData::currentMovementSpeed (+0x2a8): the frame
            ///       movement starts from it, PlayerControls (0x14079d630) reads it as the current speed and writes the
            ///       stick's target into it, and Actor_ApplyFrameMovement writes the frame's speed (ours, after this
            ///       hook) back into it for the graph's Speed. Left there, CB's speed would become the engine's next
            ///       starting point (a per-frame factor once compounded 435 -> 12868 game units/s in 1.4 s), so before the
            ///       engine computes, the state is put back to the engine's own last value, unless the engine wrote a new
            ///       one since (the stick moved).
            /// @invariant The engine never sees CB's speed as its own.
            if (driven && mh && driven->engineSpeed.have) {
                const EngineSpeed& es = driven->engineSpeed;
                const float seen = mh->currentMovementSpeed;
                if (std::fabs(seen - es.written) <= 1.0e-3f * (std::max)(1.0f, std::fabs(es.written))) {
                    if (es.written != es.engine && g_restoreLogs.fetch_add(1, std::memory_order_relaxed) < 5)
                        LOG_INFO("movement: engine speed state put back {:.1f} -> {:.1f}", seen, es.engine);
                    mh->currentMovementSpeed = es.engine;
                } else if (g_keepLogs.fetch_add(1, std::memory_order_relaxed) < 5) {
                    LOG_INFO("movement: engine speed state {:.1f} is not CB's {:.1f} - the engine set it, kept", seen, es.written);
                }
                /// @note The direction the same way: Actor_ApplyFrameMovement stores the frame's angles in
                ///       MiddleHighProcessData::rotation (+0xb0), which the actor's movement state hands back as its
                ///       current movement (0x1406a2c70 -> 0x140710870), and MovementHandlerAgentLargeDeltaIdle
                ///       (0x141210290 -> 0x1412192a0) weighs the current movement against the one asked for before it
                ///       tweens instead of moving. CB's backward direction against the forward-only input is the largest
                ///       delta there is.
                /// @invariant The engine never sees CB's direction as its own.
                if (mh->rotation.z == es.writtenDir) mh->rotation.z = es.engineDir;
            }

            RE::NiPoint2 raw(0.0f, 0.0f);
            if (controls) {
                raw = controls->data.moveInputVec;
                g_stick.x.store(raw.x, std::memory_order_relaxed);
                g_stick.y.store(raw.y, std::memory_order_relaxed);
                g_stick.at.store(Now(), std::memory_order_release);
            }
            float        cameraYaw   = 0.0f;
            const bool   hold        = controls && camera::ControlYaw(cameraYaw);
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
                if (steer != Steer::Player) driven->engineSpeed = EngineSpeed{};
                if (DiagBudget())
                    LOG_INFO("movement: {} steers the player", steer == Steer::Player ? "the player" : steer == Steer::Dialogue ? "dialogue" : "vanilla");
                lastSteer = steer;
            }
            float      stickHeading = 0.0f;
            const bool driving      = hold && steer == Steer::Player;
            const bool haveStick    = driving && StickHeadingOf(raw, cameraYaw, stickHeading);
            if (driving) RewriteInput(controls, raw);
            next(controller, deltaTime, out);
            if (driving) controls->data.moveInputVec = raw;
            if (!player) return;
            Driven&      d  = *driven;
            EngineSpeed& es = d.engineSpeed;

            const float heading   = player->GetHeading(false);
            const float full      = out->speed;
            const float engineDir = out->angles[2];
            const char* graphFlag   = hold ? GraphDrivesSpeed(player) : "";
            const bool  graphDrives = graphFlag[0] != 0;
            /// @note Diagnostic: every hand-over to the graph and back, with the speeds either side.
            static const char* lastFlag = "";
            if (graphFlag != lastFlag) {
                if (DiagBudget())
                    LOG_INFO("movement: graph {} the speed ({}) - model {:.0f}, engine frame {:.0f}", *graphFlag ? "takes" : "returns",
                             *graphFlag ? graphFlag : lastFlag, std::hypot(d.state.vx, d.state.vy), out->speed);
                lastFlag = graphFlag;
            }
            move::State& s = d.state;
            const bool   vanilla = steer == Steer::Vanilla;
            if (!hold || !d.init || graphDrives || vanilla || deltaTime <= 0.0f) {
                /// @note Off, first frame, the graph moving the actor, or vanilla steering: the model restarts from the
                ///       engine's own frame.
                const float dir = heading + out->angles[2];
                s       = move::State{ out->speed * std::sin(dir), out->speed * std::cos(dir), heading, 0.0f };
                d.init  = hold && !vanilla;
                es      = EngineSpeed{};
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
                        SpringDamperExact(s.yaw, s.yawRate, goal, move::kPlayer.facingHalflife, deltaTime);
                    }
                }
                out->speed     = 0.0f;
                out->angles[2] = 0.0f;
                Face(out, heading, s.yaw, kNoDirection, deltaTime);
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

            /// @note Keep the spring on the heading the game actually has (a teleport, a sync scene, the engine's own turns).
            s.yaw = heading + WrapPi(s.yaw - heading);

            const RE::NiPoint3 pos = player->GetPosition();
            move::Input        in;
            in.mode      = Aiming(player) ? move::Mode::Strafe : move::Mode::Orient;
            in.gait      = GaitOf(player);
            in.airborne  = Airborne(player);
            in.haveStick = haveStick;
            if (haveStick) in.stickX = std::sin(stickHeading), in.stickY = std::cos(stickHeading);
            /// @note Facing the camera is strafing toward a target far along the camera's yaw.
            if (in.mode == move::Mode::Strafe) {
                in.haveTarget = true;
                in.targetX    = pos.x + kFaceCameraReach * std::sin(cameraYaw);
                in.targetY    = pos.y + kFaceCameraReach * std::cos(cameraYaw);
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
            Slope& sl = d.slope;
            if (!sl.init) sl = Slope{ true, slopeGoal, 0.0f };
            else SpringDamperExact(sl.scale, sl.rate, slopeGoal, kSlopeSpeedHalflife, deltaTime);

            move::Caps caps;
            caps.forward = full * sl.scale;
            DirectionRatios(player, in.gait, caps);
            LogSpeeds(player, in.gait, full);

            move::Step(s, in, caps, move::kPlayer, pos.x, pos.y, deltaTime);

            const float speed = std::hypot(s.vx, s.vy);
            out->speed        = speed;
            out->angles[2]    = 0.0f;
            Face(out, heading, s.yaw, speed > 0.5f ? std::atan2(s.vx, s.vy) : kNoDirection, deltaTime);
            es = EngineSpeed{ true, full, speed, engineDir, out->angles[2] };

            move::Publish(player->GetFormID(), move::Snapshot{ s, in, caps, move::kPlayer, pos.x, pos.y });
            /// @note Diagnostic: twice a second while moving or asked to move, the model's inputs and output (a stick held
            ///       at no speed is a stall).
            static auto lastDiag = std::chrono::steady_clock::now();
            if (const auto now = std::chrono::steady_clock::now(); (speed > 1.0f || haveStick) && now - lastDiag > std::chrono::milliseconds(500)) {
                lastDiag = now;
                static constexpr const char* kGaits[4] = { "none", "walk", "run", "sprint" };
                constexpr float kRad = 57.29578f;
                if (DiagBudget())
                    LOG_INFO("movement: {} {} - stick {} deg, velocity {:.0f} at {:.0f} deg, cap {:.0f} (forward {:.0f} = engine {:.0f} at {:.0f} deg "
                             "x slope {:.2f}; L/R/B {:.2f}/{:.2f}/{:.2f}){}",
                             in.mode == move::Mode::Strafe ? "strafe" : "orient", kGaits[static_cast<int>(in.gait) & 3],
                             haveStick ? std::format("{:.0f}", WrapPi(stickHeading - s.yaw) * kRad) : std::string("none"), speed,
                             WrapPi(std::atan2(s.vx, s.vy) - s.yaw) * kRad, CapNow(caps, in, s), caps.forward, full, engineDir * kRad, sl.scale,
                             caps.left, caps.right, caps.back, in.airborne ? ", airborne" : "");
                if (DiagBudget())
                    LOG_INFO("movement: thread {} - raw stick ({:.2f}, {:.2f}), camera yaw {:.1f}, stick heading {:.1f}, body {:.1f} -> facing "
                             "{:.1f} (turn {:.1f} deg/s), moving toward {:.1f}",
                             ::GetCurrentThreadId(), raw.x, raw.y, cameraYaw * kRad, haveStick ? stickHeading * kRad : -1.0f, heading * kRad,
                             s.yaw * kRad, out->turn[2] * kRad, std::atan2(s.vx, s.vy) * kRad);
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

        /// @brief The ridden horse's character: the model's numbers and its gait ladder.
        /// @note  Starting values, to be hand-tuned against the horse's acceptance scenarios (spec H3): acceleration is
        ///        vanilla's ridden-horse rate (2 x the canter speed: 0.5 s to canter), the air turn cap Horse_Fall_MT's
        ///        rotate-while-moving (45 deg/s); the horse has no sprint ramps (accel and friction hold through a gallop).
        ///        Traction (spec H5) from rough real-world estimates, unverified, at 70 units per metre and g = 687
        ///        units/s^2: grip 0.7 g sideways (turn radius ~6 m at canter, ~11 m at gallop) and 0.6 g braking (a
        ///        sliding stop), a 1.5 m floor (turning on the hocks), dig-in past 60 deg braking up to that sliding stop
        ///        at 180 deg, the body leading with 0.08 s by at most 60 deg.
        struct HorseProfile {
            move::Profile model{
                .accel               = 900.0f,
                .sprintAccelSlow     = 900.0f,
                .sprintAccelFrom     = 0.0f,
                .sprintAccelTo       = 1.0f,
                .friction            = 3.0f,
                .sprintFrictionLow   = 3.0f,
                .sprintFrictionTo    = 1.0f,
                .brakingNoInput      = 700.0f,
                .brakingWithInput    = 350.0f,
                .facingHalflife      = 0.15f,
                .airTurnRate         = 0.7853982f,
                .lateralGrip         = 480.0f,
                .brakingGrip         = 412.0f,
                .minTurnRadius       = 105.0f,
                .digInAngle          = 1.0471976f,
                .digInBraking        = 412.0f,
                .digInFacingHalflife = 0.08f,
                .digInMaxLead        = 1.0471976f,
            };
            /// @brief Trot's speed as a fraction of the way from walk to canter (vanilla has no trot speed).
            float trotFraction = 0.4f;
            /// @brief Gamepad deflection below which the top rung is walk, then trot; at or above trotBelow it is canter.
            float walkBelow = 0.45f;
            float trotBelow = 0.85f;
        };
        constexpr HorseProfile kHorse{};

        /// @brief The gait ladder's rungs (spec H6): holding forward climbs to canter; the sprint toggle is gallop.
        enum class Rung : std::uint8_t { Walk, Trot, Canter, Gallop };

        /// @brief The highest rung the rider allows: gallop while the horse sprints (vanilla's sprint toggle), walk while
        ///        the walk toggle is on, else the stick's deflection picks it (spec H7; keyboard = full deflection).
        /// @note  "Running" is vanilla's own test (0x14079b230, the one Planner Horse Controls doubles its speed by): a
        ///        gamepad always runs, else PlayerControlsData::running (the run/walk toggle). So the walk toggle caps
        ///        keyboard riders and the deflection gamepad ones.
        Rung TopRung(RE::Actor* horse, const RE::PlayerControlsData& data, float deflection)
        {
            if (horse->AsActorState()->IsSprinting()) return Rung::Gallop;
            auto*      devices = RE::BSInputDeviceManager::GetSingleton();
            const bool running = (devices && devices->IsGamepadEnabled()) || data.running;
            if (!running || deflection < kHorse.walkBelow) return Rung::Walk;
            return deflection < kHorse.trotBelow ? Rung::Trot : Rung::Canter;
        }

        /// @brief The rung's speed from the horse's current movement type: walk and canter are its forward walk and run
        ///        speeds; trot sits trotFraction between them; gallop is the run speed of the sprint type the graph's
        ///        iState switches to while sprinting.
        float RungSpeed(RE::Actor* horse, Rung rung)
        {
            auto* process = horse->GetActorRuntimeData().currentProcess;
            auto* high    = process ? process->high : nullptr;
            if (!high) return 0.0f;
            using Dir         = RE::Movement::SPEED_DIRECTIONS;
            const auto& sp    = high->currentMovementType.defaultData.speeds;
            const float walk  = sp[Dir::kForward][RE::Movement::MaxSpeeds::kWalk];
            const float run   = sp[Dir::kForward][RE::Movement::MaxSpeeds::kRun];
            switch (rung) {
            case Rung::Walk: return walk;
            case Rung::Trot: return walk + (run - walk) * kHorse.trotFraction;
            default:         return run;
            }
        }

        /// @brief The player's horse whose frame this is, while the player rides it (sit state riding).
        /// @note  The Ridden component's activation (spec H12), until the component system exists: only then is the
        ///        horse's input the player's stick.
        RE::NiPointer<RE::Actor> RiddenHorseOf(RE::MovementControllerNPC* controller)
        {
            auto* player = RE::PlayerCharacter::GetSingleton();
            if (!player || !player->IsOnMount() || player->AsActorState()->GetSitSleepState() != RE::SIT_SLEEP_STATE::kRidingMount) return {};
            RE::NiPointer<RE::Actor> mount;
            if (!player->GetMount(mount) || !mount) return {};
            const auto& mc = mount->GetActorRuntimeData().movementController;
            if (!mc || static_cast<void*>(mc.get()) != controller) return {};
            return mount;
        }

        /// @brief The ridden horse's frame driver: the player's movement model with the horse's numbers, steered by the
        ///        stick relative to CB's camera (spec H1, H2), its cap the gait ladder's rung (H6, H7).
        /// @note  It runs after the engine computed the horse's frame (Planner Horse Controls -> Angle and Translation
        ///        Controllers) and replaces its speed, direction and yaw rate; pitch stays the engine's.
        /// @note  Unlike the player's driver it does not put the engine's speed state back: the cap comes from the
        ///        movement type, never from the engine's frame, so CB's speed written into currentMovementSpeed cannot
        ///        compound.
        /// @note  Hands the horse to vanilla while CB's camera does not hold the yaw, movement controls are disabled, a
        ///        dialogue menu is open, or the graph moves it (mount and dismount are synced clips; rear-up and stagger
        ///        are animation-driven).
        void OnHorseFrame(Frame::Next next, RE::MovementControllerNPC* controller, float deltaTime, game::MovementFrame* out)
        {
            next(controller, deltaTime, out);
            if (!out) return;
            const RE::NiPointer<RE::Actor> horse = RiddenHorseOf(controller);
            if (!horse) return;

            Driven&      d         = g_driven.Of(horse->GetFormID());
            auto*        controls  = RE::PlayerControls::GetSingleton();
            float        cameraYaw = 0.0f;
            const bool   hold      = controls && camera::ControlYaw(cameraYaw);
            const char*  graphFlag = hold ? GraphDrivesSpeed(horse.get()) : "";
            auto*        menus     = RE::ControlMap::GetSingleton();
            auto*        topics    = RE::MenuTopicManager::GetSingleton();
            const bool   vanilla   = !hold || graphFlag[0] != 0 || (menus && !menus->IsMovementControlsEnabled()) || (topics && topics->menuOpen);
            const float  heading   = horse->GetHeading(false);
            move::State& s         = d.state;
            if (vanilla || !d.init || deltaTime <= 0.0f) {
                /// @note The model restarts from the engine's own frame whenever it takes the horse back.
                const float dir = heading + out->angles[2];
                s      = move::State{ out->speed * std::sin(dir), out->speed * std::cos(dir), heading, 0.0f };
                d.init = !vanilla;
                if (vanilla || deltaTime <= 0.0f) return;
            }
            s.yaw = heading + WrapPi(s.yaw - heading);

            const RE::NiPoint2 raw          = controls->data.moveInputVec;
            const float        deflection   = (std::min)(1.0f, std::sqrt(raw.x * raw.x + raw.y * raw.y));
            float              stickHeading = 0.0f;
            const bool         haveStick    = StickHeadingOf(raw, cameraYaw, stickHeading);
            const Rung         rung         = TopRung(horse.get(), controls->data, deflection);

            move::Input in;
            in.mode      = move::Mode::Orient;
            in.gait      = rung == Rung::Gallop ? Gait::Sprint : rung == Rung::Walk ? Gait::Walk : Gait::Run;
            in.airborne  = Airborne(horse.get());
            in.haveStick = haveStick;
            if (haveStick) in.stickX = std::sin(stickHeading), in.stickY = std::cos(stickHeading);
            move::Caps caps;
            caps.forward = RungSpeed(horse.get(), rung);

            const RE::NiPoint3 pos = horse->GetPosition();
            move::Step(s, in, caps, kHorse.model, pos.x, pos.y, deltaTime);

            const float speed = std::hypot(s.vx, s.vy);
            out->speed        = speed;
            out->angles[2]    = 0.0f;
            Face(out, heading, s.yaw, speed > 0.5f ? std::atan2(s.vx, s.vy) : kNoDirection, deltaTime);
            move::Publish(horse->GetFormID(), move::Snapshot{ s, in, caps, kHorse.model, pos.x, pos.y });

            /// @note Diagnostic: twice a second while moving or asked to move.
            static std::atomic<double> lastDiag{ -1.0e9 };
            if (const double now = Now(); (speed > 1.0f || haveStick) && now - lastDiag.load(std::memory_order_relaxed) > 0.5) {
                lastDiag.store(now, std::memory_order_relaxed);
                static constexpr const char* kRungs[4] = { "walk", "trot", "canter", "gallop" };
                constexpr float kRad = 57.29578f;
                if (DiagBudget())
                    LOG_INFO("horse: {} (deflection {:.2f}) - stick {} deg, velocity {:.0f} at {:.0f} deg, cap {:.0f}{}", kRungs[static_cast<int>(rung) & 3],
                             deflection, haveStick ? std::format("{:.0f}", WrapPi(stickHeading - s.yaw) * kRad) : std::string("none"), speed,
                             WrapPi(std::atan2(s.vx, s.vy) - s.yaw) * kRad, caps.forward, in.airborne ? ", airborne" : "");
            }
        }

        [[maybe_unused]] const bool s_horseSubscribed = Frame::Subscribe(&OnHorseFrame, hooks::Priority::Late, "CharFacing horse", "horse_movement_model");

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
        float cameraYaw = 0.0f;
        if (!camera::ControlYaw(cameraYaw)) return false;
        if (Now() - g_stick.at.load(std::memory_order_acquire) > kStale) return false;
        const RE::NiPoint2 stick(g_stick.x.load(std::memory_order_relaxed), g_stick.y.load(std::memory_order_relaxed));
        return StickHeadingOf(stick, cameraYaw, heading);
    }

}  // namespace CB::feature::controller
