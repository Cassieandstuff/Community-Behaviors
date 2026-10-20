#include "PCH.h"

#include "CharFacing.h"
#include "CharGround.h"

#include <platform/external/TrueDirectionalMovement/TrueDirectionalMovementAPI.h>
#include <platform/game/Movement.h>
#include <platform/hooks/Override.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <mutex>

/// @file
/// @brief CB owns the player's yaw through True Directional Movement's API: while TDM is in its directional mode, CB
///        holds TDM's yaw control and sets the heading every frame from the facing spring (kFacingHalflife) toward the
///        stick's camera-relative direction (StickHeading) — the model the predictor uses, so prediction and facing
///        agree. TDM keeps everything else: its free-rotation camera and camera compensation, the forward-only input
///        rewrite, target lock (CB releases control outside the directional mode).
/// @note Without TDM the feature stays off (logged once). Yaw control is requested with multiplier 0, so TDM applies
///       CB's yaw at once and the turning dynamics are CB's spring, not TDM's rotation speed. Needs TDM's API V3.
/// @warning TDM answers BadThread off its own thread: every API call goes through OnTdmThread.

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
                if (p) LOG_INFO("facing: True Directional Movement API V3 acquired (TDM thread {})", p->GetTDMThreadId());
                else   LOG_WARN("facing: True Directional Movement (V3 API) not found — CB's facing stays off");
                return p;
            }();
            return api;
        }

        RE::Actor* PlayerOf(RE::MovementControllerNPC* controller)
        {
            auto* player = RE::PlayerCharacter::GetSingleton();
            if (!player) return nullptr;
            const auto& mc = player->GetActorRuntimeData().movementController;
            return mc && mc.get() == controller ? player : nullptr;
        }

        /// @brief The facing spring between frames, and whether CB holds TDM's yaw control.
        /// @note The player's frame movement runs on one thread at a time; `g_haveControl` is written only on TDM's
        ///       thread (OnTdmThread) and read by the frame to skip work while there is nothing to hold or release.
        struct Facing {
            bool  init  = false;
            float yaw   = 0.0f;   ///< unwrapped heading the spring is at
            float rate  = 0.0f;
        };
        Facing            g_facing;
        std::atomic<bool> g_haveControl{ false };
        std::atomic<bool> g_loggedThread{ false };

        /// @brief What CurrentFacing hands out, stamped with the frame that wrote it.
        struct Published {
            std::mutex  lock;
            FacingState state;
            bool        valid = false;
            double      at    = -1.0e9;
        };
        Published        g_published;
        constexpr double kStale = 0.5;

        double Now() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

        void Publish(bool valid, const FacingState& state = {})
        {
            std::scoped_lock lock(g_published.lock);
            g_published.valid = valid;
            g_published.state = state;
            g_published.at    = Now();
        }

        /// @brief Runs `fn` on TDM's thread: now when this is it, else as an SKSE task (the main thread).
        template <class Fn>
        void OnTdmThread(TDM_API::IVTDM3* api, Fn fn)
        {
            if (::GetCurrentThreadId() == api->GetTDMThreadId()) return fn();
            bool expected = false;
            if (g_loggedThread.compare_exchange_strong(expected, true))
                LOG_INFO("facing: frame movement runs on thread {}, TDM on {} — yaw calls are queued as SKSE tasks", ::GetCurrentThreadId(),
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
                    LOG_INFO("facing: yaw control {} (TDM result {})", g_haveControl ? "taken" : "refused", static_cast<int>(r));
                } else if (!want && g_haveControl) {
                    api->ReleaseYawControl(handle);
                    g_haveControl = false;
                    LOG_INFO("facing: yaw control released to TDM");
                }
                if (want && g_haveControl) {
                    float a = std::fmod(yaw, kTwoPi);
                    if (a < 0.0f) a += kTwoPi;
                    api->SetPlayerYaw(handle, a);
                }
            });
        }

        /// @brief While CB holds the yaw, the move input is CB's to rewrite: forward along the facing, at the stick's
        ///        length, as TDM does for itself.
        /// @note Why here: TDM's ProcessInput returns before its forward-only rewrite while another plugin holds the
        ///       yaw (_bYawControlledByPlugin), so the raw stick reaches the game ("down" = walk backward, against a body
        ///       that now faces the camera). The player-controls agents read PlayerControls::data.moveInputVec (+0x24)
        ///       while computing this frame (the speed path 0x14079d630 reads it there), so rewriting it before `next`
        ///       makes the frame a native forward request: forward direction, forward MOVT speed, forward animations.
        /// @note The stick comes from TDM's GetActualMovementInput, never from moveInputVec, which this has rewritten.
        void RewriteInput(TDM_API::IVTDM3* api)
        {
            const RE::NiPoint2 stick = api->GetActualMovementInput();
            const float        len   = std::sqrt(stick.x * stick.x + stick.y * stick.y);
            if (len < kStickDeadzone) return;
            auto* controls = RE::PlayerControls::GetSingleton();
            if (!controls) return;
            controls->data.moveInputVec = RE::NiPoint2(0.0f, (std::min)(1.0f, len));
        }

        void OnFrame(Frame::Next next, RE::MovementControllerNPC* controller, float deltaTime, game::MovementFrame* out)
        {
            RE::Actor* player = PlayerOf(controller);
            auto*      api    = player ? Api() : nullptr;
            if (api && g_haveControl && api->GetDirectionalMovementMode() == TDM_API::DirectionalMovementMode::kDirectional)
                RewriteInput(api);
            next(controller, deltaTime, out);
            if (!api) return;

            const float heading = player->GetHeading(false);
            Facing&     f       = g_facing;
            const bool  want    = api->GetDirectionalMovementMode() == TDM_API::DirectionalMovementMode::kDirectional;
            if (!want || !f.init) {
                f = Facing{ true, heading, 0.0f };
                if (want || g_haveControl) Control(api, want, heading);
                Publish(false);
                return;
            }
            /// @note Keep the spring on the heading the game actually has (TDM's own turns, a teleport, a sync scene).
            f.yaw = heading + WrapPi(f.yaw - heading);
            float      goal = f.yaw;
            float      stick;
            const bool haveGoal = StickHeading(stick);
            if (haveGoal) goal = f.yaw + WrapPi(stick - f.yaw);
            if (deltaTime > 0.0f) SpringDamperExact(f.yaw, f.rate, goal, kFacingHalflife, deltaTime);
            Control(api, true, f.yaw);
            Publish(true, FacingState{ f.yaw, f.rate, haveGoal, goal });
        }

        [[maybe_unused]] const bool s_subscribed = Frame::Subscribe(&OnFrame, hooks::Priority::Late, "CharFacing", "char_facing");

    }  // namespace

    bool CurrentFacing(FacingState& out)
    {
        std::scoped_lock lock(g_published.lock);
        if (!g_published.valid || Now() - g_published.at > kStale) return false;
        out = g_published.state;
        return true;
    }

    float TurnSpeedScale(float gap)
    {
        constexpr float    kPi       = 3.14159265f;
        static const float freeAngle = [] {
            const float deg = std::clamp(SettingFloat("fTurnFreeAngle", 45.0f), 0.0f, 180.0f);
            LOG_INFO("facing: full speed within {:.0f} deg of the stick, easing to none at 180", deg);
            return deg * kPi / 180.0f;
        }();
        const float a = std::fabs(gap);
        if (a <= freeAngle || freeAngle >= kPi) return 1.0f;
        const float u = (std::min)(1.0f, (a - freeAngle) / (kPi - freeAngle));
        return 1.0f - u * u * (3.0f - 2.0f * u);
    }

    bool StickHeading(float& heading)
    {
        auto* api = Api();
        auto* player = RE::PlayerCharacter::GetSingleton();
        auto* camera = RE::PlayerCamera::GetSingleton();
        if (!api || !player || !camera) return false;
        if (api->GetDirectionalMovementMode() != TDM_API::DirectionalMovementMode::kDirectional) return false;
        if (!camera->IsCurrentState(RE::CameraStates::kThirdPerson, RE::CameraStates::kVRThirdPerson)) return false;
        const RE::NiPoint2 stick = api->GetActualMovementInput();
        if (stick.x * stick.x + stick.y * stick.y < kStickDeadzone * kStickDeadzone) return false;
        const auto* third     = static_cast<const RE::ThirdPersonState*>(camera->currentState.get());
        const float cameraYaw = player->GetHeading(false) + (third->freeRotationEnabled ? third->freeRotation.x : 0.0f);
        heading               = cameraYaw + std::atan2(stick.x, stick.y);
        return true;
    }

}  // namespace CB::feature::controller
