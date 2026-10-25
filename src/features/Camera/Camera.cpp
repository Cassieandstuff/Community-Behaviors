#include "PCH.h"

#include "Camera.h"
#include "CameraRig.h"

#include <platform/hooks/VirtualSlot.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <mutex>
#include <utility>

/// @file
/// @brief CB drives the third-person camera (bCamera) through two slots of ThirdPersonState's vtable, for the player's
///        cameraStates[kThirdPerson] object only.
/// @note Vanilla's Update still runs, wrapped: it reads the look and zoom input, switches the player's model, eases the
///       zoom, the shoulder dolly and the pitch zoom, applies camera shake and the Camera Control bone's image-space
///       modifiers, and switches to first person on a full zoom-in. Around it CB owns the camera's yaw and pitch (kept
///       in world space and handed to vanilla as the free-rotation offset) and replaces the position with its rig.
/// @note The same class also serves kAnimated (killcams, applyOffsets off), which passes through untouched; Horse,
///       Bleedout and Dragon have their own vtables.

namespace CB::feature::camera {

    namespace {

        namespace hooks = platform::hooks;
        using Clock     = std::chrono::steady_clock;

        /// @warning Indices go through the three-argument REL::Relocate(se, ae, vr): the two-argument form is
        ///          (seAndVR, ae), and (0x3, 0x4) once hooked GetRotation and UpdateRotation on AE.
        struct UpdateSlot {
            static constexpr const char* name = "ThirdPersonState::Update";
            static REL::VariantID        Table() { return RE::VTABLE_ThirdPersonState[0]; }
            static std::size_t           Index() { return REL::Relocate<std::size_t>(0x3, 0x3, 0x4); }
        };
        struct FreeRotationSlot {
            static constexpr const char* name = "ThirdPersonState::SetFreeRotationMode";
            static REL::VariantID        Table() { return RE::VTABLE_ThirdPersonState[0]; }
            static std::size_t           Index() { return REL::Relocate<std::size_t>(0xD, 0xD, 0xE); }
        };
        using Update       = hooks::VirtualSlot<UpdateSlot, void(RE::ThirdPersonState*, RE::BSTSmartPointer<RE::TESCameraState>*)>;
        using FreeRotation = hooks::VirtualSlot<FreeRotationSlot, void(RE::ThirdPersonState*, bool)>;

        constexpr float  kPi       = 3.14159265f;
        constexpr float  kTwoPi    = 6.2831853f;
        constexpr float  kMaxPitch = 1.5707964f;  ///< vanilla's body pitch limit (Actor::ModifyRotationX)
        constexpr double kStale    = 0.25;        ///< seconds without a camera frame before the yaw stops being handed out

        float WrapPi(float a)
        {
            a = std::fmod(a + kPi, kTwoPi);
            if (a < 0.0f) a += kTwoPi;
            return a - kPi;
        }

        float Wrap2Pi(float a)
        {
            a = std::fmod(a, kTwoPi);
            return a < 0.0f ? a + kTwoPi : a;
        }

        /// @brief Plugins that drive the same third-person camera: with either loaded, two writers fight over the camera
        ///        every frame (seen with SmoothCam: the camera flashes between their positions).
        constexpr std::pair<const wchar_t*, const char*> kRivals[] = {
            { L"TrueDirectionalMovement.dll", "True Directional Movement" },
            { L"SmoothCam.dll", "SmoothCam" },
        };

        /// @note Decided on the first camera frame, not at plugin load, so a rival that loads after CB is seen.
        bool Enabled()
        {
            static const bool on = [] {
                if (REL::Module::IsVR()) {
                    LOG_WARN("camera: VR is not supported - vanilla's camera stays");
                    return false;
                }
                for (const auto& [dll, rival] : kRivals) {
                    if (::GetModuleHandleW(dll)) {
                        LOG_WARN("camera: {} is loaded and drives the same camera - CB's camera stays off", rival);
                        return false;
                    }
                }
                LOG_INFO("camera: CB drives the third-person camera");
                return true;
            }();
            return on;
        }

        bool Owns(const RE::TESCameraState* state)
        {
            const auto* camera = RE::PlayerCamera::GetSingleton();
            return state && camera && Enabled() && camera->GetRuntimeData().cameraStates[RE::CameraState::kThirdPerson].get() == state;
        }

        float IniFloat(std::string_view name, float def)
        {
            if (auto* ini = RE::INISettingCollection::GetSingleton())
                if (auto* s = ini->GetSetting(name)) return s->GetFloat();
            if (auto* prefs = RE::INIPrefSettingCollection::GetSingleton())
                if (auto* s = prefs->GetSetting(name)) return s->GetFloat();
            return def;
        }

        /// @brief The built-in profile: vanilla's look rate and boom lengths, so the default camera feels and frames
        ///        like vanilla's.
        const rig::Profile& DefaultProfile()
        {
            static const rig::Profile profile = [] {
                rig::Profile p;
                p.orientation.speed = IniFloat("fFreeRotationSpeed:Camera", 3.0f);
                p.boom.minDistance  = IniFloat("fVanityModeMinDist:Camera", 155.0f);
                p.boom.maxDistance  = IniFloat("fVanityModeMaxDist:Camera", 600.0f);
                LOG_INFO("camera: profile '{}' - look rate {:.2f}, boom {:.0f}..{:.0f}, anchor lag xy {:.2f} s / z {:.2f} s, collision radius {:.0f}",
                         p.name, p.orientation.speed, p.boom.minDistance, p.boom.maxDistance, p.anchor.halflifeXY, p.anchor.halflifeZ,
                         p.collision.radius);
                return p;
            }();
            return profile;
        }

        /// @brief The camera's own state, under g_lock (the update and ControlYaw's readers never share more than the
        ///        atomics below; the lock guards against a second caller of the state's Update).
        struct Live {
            rig::OrientationState view;
            rig::State            rig;
            bool                  seeded = false;
            int                   seeds  = 0;
            int                   diags  = 0;
            Clock::time_point     last{};
            Clock::time_point     lastDiag{};
        };
        Live       g_live;
        std::mutex g_lock;

        /// @brief What ControlYaw hands out, stamped with the camera frame that wrote it.
        std::atomic<float>  g_yaw{ 0.0f };
        std::atomic<double> g_at{ -1.0e9 };

        double Seconds(Clock::time_point t) { return std::chrono::duration<double>(t.time_since_epoch()).count(); }

        /// @brief What the boom's ray met.
        struct Cast {
            float clear = 1.0f;  ///< the share of the ray that is clear (1 = nothing hit)
            int   layer = -1;    ///< the hit's collision layer (COL_LAYER), -1 without a hit
        };

        /// @note On the camera-sphere layer and in the player's own system group, which the collision filter keeps
        ///       from colliding with itself: the ray starts at eye height on the shoulder, at the top of the player's
        ///       capsule, so the player must not be something it can hit.
        Cast CastBoom(RE::PlayerCharacter* player, rig::Vec3 from, rig::Vec3 to)
        {
            auto* cell  = player->GetParentCell();
            auto* world = cell ? cell->GetbhkWorld() : nullptr;
            if (!world) return {};
            RE::CFilter own;
            player->GetCollisionFilterInfo(own);
            const float     s = RE::bhkWorld::GetWorldScale();
            RE::bhkPickData pick;
            pick.rayInput.from = RE::hkVector4(from.x * s, from.y * s, from.z * s, 0.0f);
            pick.rayInput.to   = RE::hkVector4(to.x * s, to.y * s, to.z * s, 0.0f);
            pick.rayInput.filterInfo.SetCollisionLayer(RE::COL_LAYER::kCameraSphere);
            pick.rayInput.filterInfo.SetSystemGroup(own.GetSystemGroup());
            world->PickObject(pick);
            if (!pick.rayOutput.HasHit()) return {};
            return { std::clamp(pick.rayOutput.hitFraction, 0.0f, 1.0f),
                     pick.rayOutput.rootCollidable ? static_cast<int>(pick.rayOutput.rootCollidable->GetCollisionLayer()) : -1 };
        }

        void OnUpdate(Update::Next next, RE::ThirdPersonState* tps, RE::BSTSmartPointer<RE::TESCameraState>* nextState)
        {
            auto* player   = RE::PlayerCharacter::GetSingleton();
            auto* camera   = RE::PlayerCamera::GetSingleton();
            auto* controls = RE::PlayerControls::GetSingleton();
            if (!player || !controls || !Owns(tps)) return next(tps, nextState);

            std::scoped_lock    lock(g_lock);
            Live&               l       = g_live;
            const rig::Profile& profile = DefaultProfile();
            const auto          now     = Clock::now();
            const float         heading = player->GetHeading(false);
            float               dt      = static_cast<float>(std::chrono::duration<double>(now - l.last).count());
            /// @note Re-entering third person (or the first frame) starts from what vanilla shows: the body's heading
            ///       and pitch plus any free-rotation offset left over.
            if (!l.seeded || dt > kStale) {
                l.view.yaw   = Wrap2Pi(heading + (tps->freeRotationEnabled ? tps->freeRotation.x : 0.0f));
                l.view.pitch = std::clamp(player->data.angle.x - (tps->freeRotationEnabled ? tps->freeRotation.y : 0.0f), -kMaxPitch, kMaxPitch);
                l.rig        = {};
                l.seeded     = true;
                dt           = 0.0f;
                if (++l.seeds <= 20)
                    LOG_INFO("camera: third person entered - yaw {:.1f} deg, pitch {:.1f} deg", l.view.yaw * 57.29578f, l.view.pitch * 57.29578f);
            }
            l.last = now;
            dt     = (std::min)(dt, 0.1f);

            /// @note CB turns the view by the look input itself (the rig's Orientation stage, by time), so the view
            ///       is the camera's own rather than vanilla's free-rotation offset; vanilla's HandleLookInput is fed no
            ///       look while it runs, so the look is not counted twice (put back after).
            const RE::NiPoint2 look = controls->data.lookInputVec;
            const float        yawIn = l.view.yaw;
            rig::StepOrientation(profile.orientation, l.view, { look.x, look.y, dt });

            /// @note Vanilla is handed CB's yaw as the offset from the body's heading, so its UpdateRotation (yaw =
            ///       heading + freeRotation.x in free rotation) shows CB's world yaw however the body turned. Pitch goes
            ///       through the body's pitch, which the body's aim needs anyway.
            tps->freeRotationEnabled      = true;
            tps->freeRotation.x           = WrapPi(l.view.yaw - heading);
            tps->freeRotation.y           = 0.0f;
            player->data.angle.x          = l.view.pitch;
            controls->data.lookInputVec   = RE::NiPoint2(0.0f, 0.0f);
            next(tps, nextState);
            controls->data.lookInputVec   = look;
            const float headingAfter      = player->GetHeading(false);

            /// @note Vanilla's UpdatePitch writes the body's pitch itself where free rotation does not stop it
            ///       (swimming, a flying mount, gamepad aim assist); then that is the view's pitch.
            if (player->data.angle.x != l.view.pitch) l.view.pitch = std::clamp(player->data.angle.x, -kMaxPitch, kMaxPitch);
            player->data.angle.x = l.view.pitch;
            tps->freeRotation.y  = 0.0f;

            /// @note A full zoom-in switched to first person inside vanilla's Update: first person looks along the
            ///       body, so the body turns to where the camera looked.
            if (camera->currentState.get() != tps) {
                if (camera->IsInFirstPerson()) player->SetHeading(l.view.yaw);
                return;
            }

            auto* root = player->Get3D(false);
            if (!root) return;
            const float         height = tps->thirdPersonCameraObj ? tps->thirdPersonCameraObj->local.translate.z : 0.0f;
            const RE::NiPoint3& at     = root->world.translate;

            rig::Basis      basis;
            const RE::NiMatrix3 m = tps->rotation.ToRotation();
            for (int r = 0; r < 3; ++r)
                for (int c = 0; c < 3; ++c) basis.m[r][c] = m.entry[r][c];

            const rig::Vec3 anchor = rig::StepAnchor(profile.anchor, l.rig.anchor, { { at.x, at.y, at.z + height }, dt });
            rig::BoomIn     boomIn;
            boomIn.anchor    = anchor;
            boomIn.shoulder  = { tps->posOffsetActual.x, tps->posOffsetActual.y, tps->posOffsetActual.z };
            boomIn.zoom      = tps->currentZoomOffset;
            boomIn.pitchZoom = tps->pitchZoomOffset;
            boomIn.rotation  = basis;
            const rig::BoomOut boom = rig::StepBoom(profile.boom, boomIn);
            const Cast         cast = CastBoom(player, boom.pivot, boom.eye);
            const rig::Vec3    eye  = rig::StepCollision(profile.collision, l.rig.collision, { boom.pivot, boom.eye, cast.clear, dt });

            /// @note Diagnostic: twice a second, what the camera read and what it showed; and every boom hit (up to a
            ///       budget), with what it hit.
            constexpr float kDeg = 57.29578f;
            if (cast.layer >= 0 && l.diags < 600) {
                ++l.diags;
                LOG_INFO("camera: boom hit layer {} at {:.2f} of {:.0f}", cast.layer, cast.clear, rig::Length(boom.eye - boom.pivot));
            }
            if (now - l.lastDiag > std::chrono::milliseconds(500) && l.diags < 600) {
                l.lastDiag = now;
                ++l.diags;
                LOG_INFO("camera: thread {} dt {:.3f} - look ({:.2f}, {:.2f}) turned {:.2f}, yaw {:.1f} deg, body {:.1f} -> {:.1f} across "
                         "vanilla, pitch {:.1f}, zoom {:.2f}, boom {:.0f} shown {:.0f} (clear {:.2f})",
                         ::GetCurrentThreadId(), dt, look.x, look.y, WrapPi(l.view.yaw - yawIn) * kDeg, l.view.yaw * kDeg, heading * kDeg,
                         headingAfter * kDeg, l.view.pitch * kDeg, tps->currentZoomOffset, rig::Length(boom.eye - boom.pivot),
                         rig::Length(eye - boom.pivot), cast.clear);
            }

            tps->translation = RE::NiPoint3(eye.x, eye.y, eye.z);
            if (auto* node = camera->cameraRoot.get()) {
                node->local.translate = tps->translation;
                RE::NiUpdateData update;
                node->Update(update);
            }

            g_yaw.store(l.view.yaw, std::memory_order_relaxed);
            g_at.store(Seconds(now), std::memory_order_release);
        }

        /// @note Vanilla allows free rotation only while sheathed and standing, and leaving it turns the body to the
        ///       camera (Actor::ModifyRotationZ's only caller): the snap. CB's camera keeps it on, so neither happens.
        void OnFreeRotation(FreeRotation::Next next, RE::ThirdPersonState* tps, bool sheathed)
        {
            if (!Owns(tps)) return next(tps, sheathed);
            tps->freeRotationEnabled = true;
        }

        [[maybe_unused]] const bool s_update       = Update::Subscribe(&OnUpdate, hooks::Priority::Normal, "Camera", "camera");
        [[maybe_unused]] const bool s_freeRotation = FreeRotation::Subscribe(&OnFreeRotation, hooks::Priority::Normal, "Camera", "camera");

    }  // namespace

    bool ControlYaw(float& yaw)
    {
        const auto* camera = RE::PlayerCamera::GetSingleton();
        if (!camera || !camera->IsCurrentState(RE::CameraState::kThirdPerson, RE::CameraState::kVRThirdPerson)) return false;
        if (Seconds(Clock::now()) - g_at.load(std::memory_order_acquire) > kStale) return false;
        yaw = g_yaw.load(std::memory_order_relaxed);
        return true;
    }

}  // namespace CB::feature::camera
