#include "PCH.h"

#include "Camera.h"
#include "CameraRig.h"

#include "features/Advanced Controller/CharSweep.h"

#include <Common/Base/Types/Geometry/Aabb/hkAabb.h>
#include <Physics/Collide/Shape/hkpShape.h>
#include <core/debug/Settings.h>
#include <platform/game/Camera.h>
#include <platform/hooks/CallSite.h>
#include <platform/hooks/VirtualSlot.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cmath>
#include <mutex>
#include <utility>

/// @file
/// @brief CB drives the third-person camera (bCamera) through two slots of ThirdPersonState's vtable, for the player's
///        cameraStates[kThirdPerson] object only, and aims the crosshair pick along it (one call site). Riding keeps
///        third person current: the Mounted component frames the rider instead of vanilla's HorseCameraState.
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
        constexpr const char* kBodyNode = "NPC COM [COM ]";  ///< exact: the trailing space is the skeleton's

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

        RE::ThirdPersonState* HorseState(RE::PlayerCamera* camera)
        {
            return static_cast<RE::ThirdPersonState*>(camera->GetRuntimeData().cameraStates[RE::CameraState::kMount].get());
        }

        /// @brief CB kept third person current through the mount (OnMountPush) and owes vanilla's mount end.
        std::atomic<bool> g_mountTaken{ false };

        /// @brief How far above `actor`'s root vanilla's camera pivots on it; 0 where the function is not pinned.
        float CameraHeight(RE::Actor* actor)
        {
            return hooks::engine::Available(hooks::engine::Actor_GetCameraHeight) ? platform::game::Actor_GetCameraHeight(actor) : 0.0f;
        }

        /// @brief What ExitMountCamera does when the horse state is current, less what CB's camera keeps: the pop, the
        ///        retarget, the freeRotation reset and the transition blend.
        void ReleaseMount(RE::PlayerCamera* camera, const char* why)
        {
            RE::HUDData::GenerateHUDMessage("HorseMode", false);
            if (auto* horse = HorseState(camera)) {
                RE::RefHandle none = 0;
                horse->SetCameraHandle(none);
            }
            LOG_INFO("camera: mount ended ({}) - Mounted component easing out", why);
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
                p.collision.passThin = !core::debug::FlagOn("camera_collide_thin");
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
            rig::EaseState        mounted;
            rig::Vec3             mountShoulder;
            bool                  mountShoulderInit = false;
            RE::ActorHandle       mount;       ///< the last mount CB framed; kept through the ease out
            rig::Vec3             mountPivot;  ///< its last pivot, for when its 3D is gone
            rig::EaseState        body;        ///< how far the rider's pivot follows the body rather than the root
            rig::Vec3             comOffset;   ///< the body (NPC COM) from the on-foot pivot, measured on foot
            int                   mountDiags = 0;
            Clock::time_point     lastMountDiag{};
            bool                  comInit = false;
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
            bool  thin  = false; ///< the hit is a thin object
        };

        namespace hk = CB::havok;

        constexpr std::uint64_t LayerBit(RE::COL_LAYER layer) { return 1ull << static_cast<unsigned>(layer); }

        /// @brief Layers the camera passes through: bodies and what they drop. Vanilla's caster skips the living ones
        ///        (0x14054f720) and stops on debris, weapons and corpses; CB's does not.
        constexpr std::uint64_t kPassLayers = LayerBit(RE::COL_LAYER::kCharController) | LayerBit(RE::COL_LAYER::kBiped) |
                                              LayerBit(RE::COL_LAYER::kBipedNoCC) | LayerBit(RE::COL_LAYER::kDeadBip) |
                                              LayerBit(RE::COL_LAYER::kWeapon) | LayerBit(RE::COL_LAYER::kDebrisSmall) |
                                              LayerBit(RE::COL_LAYER::kDebrisLarge) | LayerBit(RE::COL_LAYER::kShellCasting) |
                                              LayerBit(RE::COL_LAYER::kCollisionBox);

        /// @note The trees layer, or bounds whose second-largest extent is small: a pole or a railing qualifies, a wall
        ///       (long and tall) does not however thin it is. World AABBs overestimate a tilted object, which then stays
        ///       solid: the safe side. The shape's getAabb is vtable slot 7 (see CharGround.cpp).
        bool IsThin(const hk::hkpCollidable* body, float widthHavok)
        {
            const unsigned layer = body->getCollisionFilterInfo() & 0x7f;
            if (layer == static_cast<unsigned>(RE::COL_LAYER::kTrees)) return true;
            if (layer == static_cast<unsigned>(RE::COL_LAYER::kTerrain) || layer == static_cast<unsigned>(RE::COL_LAYER::kGround)) return false;
            const hk::hkpShape* shape = body->getShape();
            if (!shape || !body->m_motion) return false;
            hk::hkAabb aabb;
            using Fn      = void (*)(const hk::hkpShape*, const hk::hkTransform&, float, hk::hkAabb&);
            const auto fn = reinterpret_cast<Fn>((*reinterpret_cast<void* const* const*>(shape))[7]);
            fn(shape, body->getTransform(), 0.0f, aabb);
            float e[3] = { aabb.m_max(0) - aabb.m_min(0), aabb.m_max(1) - aabb.m_min(1), aabb.m_max(2) - aabb.m_min(2) };
            std::sort(e, e + 3);
            return e[1] < widthHavok;
        }

        struct ThinFilter {
            float widthHavok  = 0.0f;
            float keepOut     = 0.0f;  ///< a thin hit counts this close to the path's end, as a share of the path
            bool  sawThinBest = false;
        };

        /// @note A thin hit counts only where the eye would sit in it.
        bool AcceptHit(void* user, const hk::hkpCollidable* body, float fraction)
        {
            auto&      f    = *static_cast<ThinFilter*>(user);
            const bool thin = body && IsThin(body, f.widthHavok);
            if (thin && fraction < 1.0f - f.keepOut) return false;
            f.sawThinBest = thin;
            return true;
        }

        /// @brief Sweeps vanilla's camera sphere (PlayerCamera+0x120's phantom) from `from` to `to`, without moving it.
        /// @note Vanilla sets the sphere's radius on this same chain before each cast (0x14054f720), so writing it here
        ///       is safe: ApplyCameraCaster sets its own again.
        Cast SweepBoom(RE::PlayerCharacter* player, RE::PlayerCamera* camera, const rig::CollisionParams& p, rig::Vec3 from, rig::Vec3 to)
        {
            auto* cell   = player->GetParentCell();
            auto* world  = cell ? cell->GetbhkWorld() : nullptr;
            auto* caster = camera->GetRuntimeData().unk120;
            auto* bhk    = caster ? caster->unk00.get() : nullptr;
            auto* object = bhk ? bhk->referencedObject.get() : nullptr;
            if (!world || !object) return {};
            const float s      = RE::bhkWorld::GetWorldScale();
            const float length = rig::Length(to - from);
            if (length <= 1.0e-3f) return {};
            const auto* collidable = reinterpret_cast<const hk::hkpCollidable*>(reinterpret_cast<const std::uint8_t*>(object) + 0x20);
            const auto* wrapper    = reinterpret_cast<const std::uint8_t*>(collidable->getShape());
            const auto* child      = wrapper ? *reinterpret_cast<const std::uint8_t* const*>(wrapper + 0x10) : nullptr;
            auto*       sphere     = child ? *reinterpret_cast<std::uint8_t* const*>(child + 0x10) : nullptr;
            if (!sphere) return {};
            *reinterpret_cast<float*>(sphere + 0x20) = p.radius * s;

            ThinFilter filter{ p.thinWidth * s, (std::min)(1.0f, 2.0f * p.radius / length) };
            const controller::HitFilter hitFilter{ &AcceptHit, &filter };
            hk::hkVector4 start, end;
            start.set(from.x * s, from.y * s, from.z * s, 0.0f);
            end.set(to.x * s, to.y * s, to.z * s, 0.0f);
            const controller::SweepHit hit = controller::SweepFrom(reinterpret_cast<hk::bhkWorld*>(world), collidable, start, end, 0.0f, 2.0f,
                                                                   kPassLayers, p.passThin ? &hitFilter : nullptr);
            if (!hit.hit) return {};
            return { std::clamp(hit.fraction, 0.0f, 1.0f), hit.body ? static_cast<int>(hit.body->getCollisionFilterInfo() & 0x7f) : -1,
                     p.passThin && filter.sawThinBest };
        }

        /// @brief Vanilla's own Update is inside OnUpdate: its caster call is skipped there (OnVanillaCaster).
        thread_local bool t_inOwnedUpdate = false;

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

            /// @note A load or a teleport can end the ride without the dismount's StopHorseCamera.
            const auto sit = player->AsActorState()->GetSitSleepState();
            if (g_mountTaken.load(std::memory_order_relaxed) && sit == RE::SIT_SLEEP_STATE::kNormal && !player->IsOnMount() &&
                g_mountTaken.exchange(false))
                ReleaseMount(camera, "no longer riding");
            /// @note Eases in from the mount's start (its paired clip plays meanwhile) and out from the dismount's.
            const float mountTarget = g_mountTaken.load(std::memory_order_relaxed) && sit != RE::SIT_SLEEP_STATE::kWantToStand ? 1.0f : 0.0f;
            /// @note Through the mount and dismount clips the rider's root stays put (on the ground mounting, on the
            ///       saddle dismounting, until the clip ends and the rider is moved to the body), so the rider's pivot
            ///       follows the body meanwhile.
            const bool  clip       = g_mountTaken.load(std::memory_order_relaxed) &&
                              (sit == RE::SIT_SLEEP_STATE::kWantToSit || sit == RE::SIT_SLEEP_STATE::kWaitingForSitAnim ||
                               sit == RE::SIT_SLEEP_STATE::kWantToStand);
            const float bodyTarget = clip ? 1.0f : 0.0f;
            /// @note Re-entering third person (or the first frame) starts from what vanilla shows: the body's heading
            ///       and pitch plus any free-rotation offset left over.
            if (!l.seeded || dt > kStale) {
                l.view.yaw   = Wrap2Pi(heading + (tps->freeRotationEnabled ? tps->freeRotation.x : 0.0f));
                l.view.pitch = std::clamp(player->data.angle.x - (tps->freeRotationEnabled ? tps->freeRotation.y : 0.0f), -kMaxPitch, kMaxPitch);
                l.rig        = {};
                l.mounted    = { mountTarget, 0.0f };
                l.body       = { bodyTarget, 0.0f };
                l.seeded     = true;
                dt           = 0.0f;
                if (++l.seeds <= 20)
                    LOG_INFO("camera: third person entered - yaw {:.1f} deg, pitch {:.1f} deg", l.view.yaw * 57.29578f, l.view.pitch * 57.29578f);
            }
            l.last = now;
            dt     = (std::min)(dt, 0.1f);
            const float mounted = rig::StepEase(profile.mounted.ease, l.mounted, mountTarget, dt);
            const float body    = rig::StepEase(profile.mounted.body, l.body, bodyTarget, dt);

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
            t_inOwnedUpdate = true;
            next(tps, nextState);
            t_inOwnedUpdate = false;
            controls->data.lookInputVec   = look;
            const float headingAfter      = player->GetHeading(false);

            /// @note The mounted direct-at aim (BSDirectAtModifierInterface 0x1407c6ad0) looks along the rider's heading
            ///       plus the HORSE state's freeRotation.x, current or not: it is handed CB's yaw.
            if (g_mountTaken.load(std::memory_order_relaxed))
                if (auto* horse = HorseState(camera)) horse->freeRotation.x = WrapPi(l.view.yaw - headingAfter);

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

            /// @note Mounted, the pivot moves to vanilla's mounted one: the mount's root plus its camera height (a horse
            ///       has no camera node), not the rider's, whose root rides the saddle.
            rig::Vec3   pivot{ at.x, at.y, at.z + height };
            float       comZ = 0.0f;
            /// @note The body's offset is taken from the whole on-foot pivot, camera height included, so following
            ///       the body leaves out what the mount and dismount clips key on Camera3rd.
            if (const auto* com = root->GetObjectByName(kBodyNode)) {
                const RE::NiPoint3& c = com->world.translate;
                const rig::Vec3     offset{ c.x - pivot.x, c.y - pivot.y, c.z - pivot.z };
                comZ                      = c.z;
                if (!g_mountTaken.load(std::memory_order_relaxed) && sit == RE::SIT_SLEEP_STATE::kNormal) {
                    if (!l.comInit) l.comOffset = offset;
                    l.comInit     = true;
                    const float h = profile.mounted.comHalflife;
                    l.comOffset   = { rig::Damp(l.comOffset.x, offset.x, h, dt), rig::Damp(l.comOffset.y, offset.y, h, dt),
                                      rig::Damp(l.comOffset.z, offset.z, h, dt) };
                }
                if (body > 1.0e-3f && l.comInit)
                    pivot = rig::Lerp(pivot, rig::Vec3{ c.x, c.y, c.z } - l.comOffset, body);
            }
            const rig::Vec3 rider = pivot;
            if (mounted > 1.0e-3f) {
                const auto  mount   = l.mount.get();
                const auto* mount3D = mount ? mount->Get3D(false) : nullptr;
                if (mount3D) {
                    const RE::NiPoint3& m = mount3D->world.translate;
                    l.mountPivot          = { m.x, m.y, m.z + CameraHeight(mount.get()) + profile.mounted.height };
                }
                pivot = rig::Lerp(pivot, l.mountPivot, mounted);
            }
            /// @note Diagnostic: ten times a second around a mount, the terms the pivot is made of.
            if ((g_mountTaken.load(std::memory_order_relaxed) || body > 1.0e-3f || mounted > 1.0e-3f) && l.mountDiags < 800 &&
                now - l.lastMountDiag > std::chrono::milliseconds(100)) {
                l.lastMountDiag = now;
                ++l.mountDiags;
                LOG_INFO("camera: mount pivot - sit {} body {:.2f} mounted {:.2f}: root z {:.0f} + camera height {:.0f}, body z {:.0f} "
                         "(offset z {:.0f}) -> rider ({:.0f}, {:.0f}, {:.0f}); mount ({:.0f}, {:.0f}, {:.0f}) -> pivot ({:.0f}, {:.0f}, {:.0f})",
                         static_cast<int>(sit), body, mounted, at.z, height, comZ, l.comOffset.z, rider.x, rider.y, rider.z, l.mountPivot.x,
                         l.mountPivot.y, l.mountPivot.z, pivot.x, pivot.y, pivot.z);
            }
            const rig::Vec3 anchor = rig::StepAnchor(profile.anchor, l.rig.anchor, { pivot, dt });
            rig::BoomIn     boomIn;
            boomIn.anchor    = anchor;
            boomIn.shoulder  = { tps->posOffsetActual.x, tps->posOffsetActual.y, tps->posOffsetActual.z };
            /// @note The horse state keeps its offset current while it is not (UpdateThirdPerson broadcasts weapon
            ///       draws to it; StartMountCamera handed it the mount).
            auto* horse = mounted > 1.0e-3f ? HorseState(camera) : nullptr;
            if (horse) {
                const rig::Vec3 want{ horse->posOffsetExpected.x, horse->posOffsetExpected.y, horse->posOffsetExpected.z };
                if (!l.mountShoulderInit) l.mountShoulder = want;
                l.mountShoulderInit = true;
                const float h       = profile.mounted.shoulderHalflife;
                l.mountShoulder     = { rig::Damp(l.mountShoulder.x, want.x, h, dt), rig::Damp(l.mountShoulder.y, want.y, h, dt),
                                        rig::Damp(l.mountShoulder.z, want.z, h, dt) };
                boomIn.shoulder     = rig::Lerp(boomIn.shoulder, l.mountShoulder, mounted);
            } else {
                l.mountShoulderInit = false;
            }
            boomIn.zoom      = tps->currentZoomOffset;
            boomIn.pitchZoom = tps->pitchZoomOffset;
            boomIn.rotation  = basis;
            const rig::BoomOut boom = rig::StepBoom(profile.boom, boomIn);

            /// @note Two legs: the shoulder can reach through a wall the player stands against, where a sweep starting at
            ///       the pivot would miss it from behind. So the pivot is swept out from the anchor first.
            const Cast      shoulderCast = SweepBoom(player, camera, profile.collision, anchor, boom.pivot);
            const rig::Vec3 pivotSafe    = rig::StepCollision(profile.collision, l.rig.shoulder, { anchor, boom.pivot, shoulderCast.clear, shoulderCast.thin, dt });
            const rig::Vec3 eyeWant      = pivotSafe + (boom.eye - boom.pivot);
            const Cast      cast         = SweepBoom(player, camera, profile.collision, pivotSafe, eyeWant);
            const rig::Vec3 eye          = rig::StepCollision(profile.collision, l.rig.collision, { pivotSafe, eyeWant, cast.clear, cast.thin, dt });

            /// @note Vanilla's caster only for what it does besides its position: fading the player and the actors at the
            ///       camera, here at the eye CB shows. The position it answers is dropped.
            RE::NiPoint3 fadeAt(eye.x, eye.y, eye.z);
            platform::game::PlayerCamera_ApplyCameraCaster(camera, &fadeAt, true);

            /// @note Diagnostic: twice a second, what the camera read and what it showed; and every boom hit (up to a
            ///       budget), with what it hit.
            constexpr float kDeg = 57.29578f;
            if (cast.layer >= 0 && l.diags < 600) {
                ++l.diags;
                LOG_INFO("camera: boom hit layer {}{} at {:.2f} of {:.0f}", cast.layer, cast.thin ? " (thin)" : "", cast.clear, rig::Length(boom.eye - boom.pivot));
            }
            if (now - l.lastDiag > std::chrono::milliseconds(500) && l.diags < 600) {
                l.lastDiag = now;
                ++l.diags;
                LOG_INFO("camera: thread {} dt {:.3f} - look ({:.2f}, {:.2f}) turned {:.2f}, yaw {:.1f} deg, body {:.1f} -> {:.1f} across "
                         "vanilla, pitch {:.1f}, zoom {:.2f}, boom {:.0f} shown {:.0f} (clear {:.2f}), mounted {:.2f}",
                         ::GetCurrentThreadId(), dt, look.x, look.y, WrapPi(l.view.yaw - yawIn) * kDeg, l.view.yaw * kDeg, heading * kDeg,
                         headingAfter * kDeg, l.view.pitch * kDeg, tps->currentZoomOffset, rig::Length(boom.eye - boom.pivot),
                         rig::Length(eye - boom.pivot), cast.clear, mounted);
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

        /// @brief The player's per-frame crosshair pick asking PlayerCamera::IsInFreeRotation (0x140764310).
        using AimPickFree = hooks::CallSite<hooks::engine::AimPick_IsInFreeRotationCall, bool(RE::PlayerCamera*)>;

        /// @note In free rotation the pick casts from the body's eye along its heading and pitch, else from the camera
        ///       along its view. CB keeps free rotation on only to own the camera's yaw (OnFreeRotation), so it answers
        ///       "not free" here: the crosshair and its target follow the camera. The other readers keep the true answer:
        ///       turned off, the player-turning and Planner Player Controls paths would turn the body with the look stick.
        bool OnAimPickFree(AimPickFree::Next next, RE::PlayerCamera* camera)
        {
            if (float yaw = 0.0f; ControlYaw(yaw)) return false;
            return next(camera);
        }

        /// @brief StartMountCamera's push of the horse (or dragon) state (0x1408e3da0).
        using MountPush = hooks::CallSite<hooks::engine::MountCamera_PushCameraStateCall, void(RE::PlayerCamera*, RE::CameraState)>;

        /// @note Riding a horse keeps CB's third person current (the Mounted component frames it), so the transition
        ///       from foot to saddle is one camera. What StartMountCamera did before the push stays (the horse state
        ///       holds the mount: its offsets and the direct-at aim read it), and so does the HUD's "HorseMode" after;
        ///       only the camera's target goes back to the rider, whom CB's rig follows. Dragons keep vanilla's.
        void OnMountPush(MountPush::Next next, RE::PlayerCamera* camera, RE::CameraState state)
        {
            auto* player = RE::PlayerCharacter::GetSingleton();
            const auto& states = camera->GetRuntimeData().cameraStates;
            if (state != RE::CameraState::kMount || !player || camera->currentState.get() != states[RE::CameraState::kThirdPerson].get() ||
                !Owns(states[RE::CameraState::kThirdPerson].get()))
                return next(camera, state);
            const auto mount = camera->cameraTarget.get();
            {
                std::scoped_lock lock(g_lock);
                g_live.mount = camera->cameraTarget;
                if (const auto* mount3D = mount ? mount->Get3D(false) : nullptr) {
                    const RE::NiPoint3& m = mount3D->world.translate;
                    g_live.mountPivot     = { m.x, m.y, m.z + CameraHeight(mount.get()) + DefaultProfile().mounted.height };
                }
            }
            camera->cameraTarget = RE::ActorHandle(player);
            g_mountTaken.store(true, std::memory_order_relaxed);
            const auto* horse = HorseState(camera);
            LOG_INFO("camera: mount started - third person stays current, Mounted component easing in (mount offset {:.0f}, {:.0f}, {:.0f}, "
                     "mount camera height {:.0f})",
                     horse ? horse->posOffsetExpected.x : 0.0f, horse ? horse->posOffsetExpected.y : 0.0f,
                     horse ? horse->posOffsetExpected.z : 0.0f, mount ? CameraHeight(mount.get()) : 0.0f);
        }

        /// @brief The player's mount end calling ExitMountCamera (0x14074b3d0).
        using MountExit = hooks::CallSite<hooks::engine::MountEnd_ExitMountCameraCall, void(RE::PlayerCamera*)>;

        /// @note Vanilla's exit does nothing unless the horse or dragon state is current, so with CB's third person
        ///       current it falls through and CB does the rest.
        void OnMountExit(MountExit::Next next, RE::PlayerCamera* camera)
        {
            next(camera);
            if (!g_mountTaken.exchange(false)) return;
            ReleaseMount(camera, "dismounted");
        }

        /// @brief ThirdPersonState's position math calling PlayerCamera::ApplyCameraCaster (0x1408e8640).
        using VanillaCaster = hooks::CallSite<hooks::engine::ThirdPersonPosition_ApplyCameraCasterCall, bool(RE::PlayerCamera*, RE::NiPoint3*, bool)>;

        /// @note The same code serves kAnimated and the other third-person states, so it is skipped only inside CB's
        ///       update: vanilla's position is discarded there and its fades would fight the ones at CB's eye. "No hit"
        ///       takes vanilla's recovery path, which is idle.
        bool OnVanillaCaster(VanillaCaster::Next next, RE::PlayerCamera* camera, RE::NiPoint3* position, bool fade)
        {
            return t_inOwnedUpdate ? false : next(camera, position, fade);
        }

        [[maybe_unused]] const bool s_vanillaCaster = VanillaCaster::Subscribe(&OnVanillaCaster, hooks::Priority::Normal, "Camera", "camera");
        [[maybe_unused]] const bool s_update       = Update::Subscribe(&OnUpdate, hooks::Priority::Normal, "Camera", "camera");
        [[maybe_unused]] const bool s_freeRotation = FreeRotation::Subscribe(&OnFreeRotation, hooks::Priority::Normal, "Camera", "camera");
        [[maybe_unused]] const bool s_aimPick      = AimPickFree::Subscribe(&OnAimPickFree, hooks::Priority::Normal, "Camera", "camera");
        [[maybe_unused]] const bool s_mountPush    = MountPush::Subscribe(&OnMountPush, hooks::Priority::Normal, "Camera", "camera");
        [[maybe_unused]] const bool s_mountExit    = MountExit::Subscribe(&OnMountExit, hooks::Priority::Normal, "Camera", "camera");

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
