#include "PCH.h"

#include "CharDynamicMover.h"

#include <core/debug/Settings.h>
#include <platform/hooks/Override.h>
#include <platform/physics/GameWorld.h>

#include <Common/Base/hkBase.h>
#include <Physics/Collide/Shape/Convex/Capsule/hkpCapsuleShape.h>
#include <Physics/Collide/Agent/Collidable/hkpCollidableQualityType.h>
#include <Physics/Dynamics/Entity/hkpRigidBody.h>
#include <Physics/Dynamics/Entity/hkpRigidBodyCinfo.h>
#include <Physics/Dynamics/World/hkpWorld.h>
#include <Physics/Dynamics/World/Listener/hkpWorldPostSimulationListener.h>
#include <BSHavok/bhkCharProxyController.h>
#include <BSHavok/bhkWorld.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <mutex>

/// @file
/// @brief The player's dynamic mover's body, listener and per-step glue (CharDynamicMover.h).

namespace CB::feature::controller::dynamic {

    namespace {

        namespace lc = hky::locomotion;

        using Integrate = platform::hooks::Override<&::bhkCharProxyController::Unk_0B>;

        constexpr float kTeleport = 1.0f;   ///< Havok units between the proxy and the body's feet that mean a teleport

        std::atomic<std::uint32_t> g_logs{ 0 };
        bool LogBudget() { return g_logs.fetch_add(1, std::memory_order_relaxed) < 300; }

        /// @brief settings.ini `[Character]` tunables, read once (Box3D's defaults otherwise).
        struct Tuning {
            float mass        = SettingFloat("fMoverMass", 80.0f);
            float groundForce = SettingFloat("fMoverGroundForce", 6000.0f);
            float airForce    = SettingFloat("fMoverAirForce", 1500.0f);
            float pogoHertz   = SettingFloat("fMoverPogoHertz", 3.0f);
            float pogoDamping = SettingFloat("fMoverPogoDamping", 0.8f);
            float slideTime   = SettingFloat("fMoverSlideTime", 0.2f);
        };

        const Tuning& Tune()
        {
            static const Tuning t;
            return t;
        }

        /// @brief The player's one mover.
        /// @note `lock` guards everything the listener reads or writes (the physics step's thread) against the
        ///       controller step (the game's): the state, the ground, the request and the body's pointer.
        struct Slot {
            std::mutex                  lock;
            const void*                 ctrl  = nullptr;
            ::bhkWorld*                 world = nullptr;   ///< held by a reference while the body is in it
            ::hkpWorld*                 hk    = nullptr;
            ::hkpRigidBody*             body  = nullptr;
            lc::DynamicMoverSettings    settings;
            lc::MoverState              state;
            lc::MoverGround             ground;
            float                       dx = 0, dy = 0;
            bool                        jumped = false;   ///< latched by the listener when a substep launched
            std::atomic<bool>           claimed{ false };
            /// @brief Diagnostics: the listener's calls (for our world, for another, with no step time) and the last step
            ///        time, since the last report.
            int                         calls = 0, otherWorld = 0, noTime = 0;
            float                       lastH = 0;
            double                      reportAt = 0;
        };

        Slot& S()
        {
            static Slot s;
            return s;
        }

        /// @brief Runs the joints after every Havok step of the world the body is in: the next step integrates them.
        class Listener final : public ::hkpWorldPostSimulationListener {
        public:
            void postSimulationCallback(::hkpWorld* world) override
            {
                Slot&            s = S();
                std::scoped_lock lock(s.lock);
                if (!s.body) return;
                if (world != s.hk) {
                    ++s.otherWorld;
                    return;
                }
                const float h = world->m_dynamicsStepInfo.m_stepInfo.m_deltaTime;
                ++s.calls;
                s.lastH = h;
                if (!(h > 0.0f)) {
                    ++s.noTime;
                    return;
                }
                const ::hkVector4& p = s.body->getPosition();
                const ::hkVector4& v = s.body->getLinearVelocity();
                const float        half = s.settings.HalfSegment();
                float              vx = v(0), vy = v(1), vz = v(2);
                const bool         was = s.state.jumping;
                lc::MoverSubstep(s.settings, s.state, s.ground, s.dx, s.dy, p(0), p(1), p(2) - half, vx, vy, vz, h);
                if (!was && s.state.jumping) s.jumped = true;
                ::hkVector4 out;
                out.set(vx, vy, vz, 0.0f);
                s.body->setLinearVelocity(out);
                ::hkVector4 still;
                still.setZero4();
                s.body->setAngularVelocity(still);
            }
        };

        Listener& TheListener()
        {
            static Listener l;
            return l;
        }

        /// @brief Takes the body out of its world, drops it and the world's reference, and stops listening there.
        /// @pre `s.lock` is not held (the world lock is taken first, as the step takes it before calling the listener).
        void Release(Slot& s, const char* why)
        {
            ::hkpRigidBody* body;
            ::hkpWorld*     hk;
            ::bhkWorld*     world;
            {
                std::scoped_lock lock(s.lock);
                body = s.body, hk = s.hk, world = s.world;
                s.body = nullptr, s.hk = nullptr, s.world = nullptr, s.ctrl = nullptr;
                s.state = {}, s.ground = {}, s.jumped = false;
            }
            if (!body) return;
            {
                const ::bhkWorldLock wl(hk, 1);
                /// @note A cell's teardown may have taken the body out already; removing it again would corrupt the world.
                if (body->getWorld() == hk) {
                    ::hkpEntity* e = body;
                    hk->removeEntityBatch(&e, 1);
                }
                hk->removeWorldPostSimulationListener(&TheListener());
            }
            body->removeReference();
            /// @note The world's last reference may be ours: it is let go on the main thread, never inside a controller's
            ///       step.
            if (world) {
                if (auto* tasks = SKSE::GetTaskInterface()) tasks->AddTask([world] { world->DecRefCount(); });
                else world->DecRefCount();
            }
            if (LogBudget()) LOG_INFO("dynamic mover: body removed ({})", why);
        }

        /// @brief Makes the body for `hull` in its world: a capsule from feet + float to the hull's top, on the
        ///        phantom's collision filter (its layer and system group) and owner data, its feet at the proxy's.
        bool Create(Slot& s, ::bhkCharacterController& ctrl, const Hull& hull)
        {
            ::hkpWorld* hk = hull.world ? hull.world->GetWorld2() : nullptr;
            if (!hk || !hull.collidable) return false;
            lc::DynamicMoverSettings set;
            set.radius      = hull.radius;
            set.height      = (std::max)(hull.topZ - hull.feetZ, 4.0f * hull.radius);
            set.floatHeight = ctrl.m_stepHeight;
            set.stepDown    = set.RestLength();
            const Tuning& t = Tune();
            set.mass = t.mass, set.maxGroundForce = t.groundForce, set.maxAirForce = t.airForce;
            set.pogoHertz = t.pogoHertz, set.pogoDamping = t.pogoDamping, set.slideDuration = t.slideTime;
            set.control          = lc::MoverControl::Spring;
            /// @note The mover integrates gravity itself: Skyrim's world never moved this MOTION_CHARACTER body by its
            ///       gravity (a jump's vz held +4.68 into the sky with the world's gravity at -9.81). Falling is heavier,
            ///       as vanilla InAir's (fInAirFallingCharGravityMult).
            set.applyGravity     = true;
            set.fallGravityScale = IniFloat("fInAirFallingCharGravityMult:HAVOK", 1.35f);
            set.nativeFilter     = hull.collidable->getCollisionFilterInfo();
            set.walkableMaterial = [](std::uint32_t id) { return StairsMaterial(id); };
            const float half = set.HalfSegment();

            ::hkVector4 a, b;
            a.set(0.0f, 0.0f, -half, 0.0f);
            b.set(0.0f, 0.0f, half, 0.0f);
            auto* shape = new ::hkpCapsuleShape(a, b, set.radius);
            ::hkpRigidBodyCinfo ci;
            ci.m_shape               = shape;
            ci.m_motionType          = ::hkpMotion::MOTION_CHARACTER;
            ci.m_mass                = set.mass;
            ci.m_position.set(hull.cx, hull.cy, hull.feetZ + set.RestLength() + half, 0.0f);
            ci.m_friction            = 0.0f;
            ci.m_restitution         = 0.0f;
            ci.m_linearDamping       = 0.0f;
            ci.m_gravityFactor       = 0.0f;
            ci.m_collisionFilterInfo = hull.collidable->getCollisionFilterInfo();
            ci.m_enableDeactivation  = false;
            ci.m_qualityType         = ::HK_COLLIDABLE_QUALITY_CHARACTER;
            auto* body = new ::hkpRigidBody(ci);
            shape->removeReference();
            /// @note No owner data (0): the phantom's would make Bethesda's rigid-body contact paths (IsHurtfulBody,
            ///       ProcessHurtfulBody) read a phantom wrapper as a bhkRigidBody. Unproven either way; 0 is the safe side.
            {
                const ::bhkWorldLock wl(hk, 1);
                ::hkpEntity* e = body;
                hk->addEntityBatch(&e, 1, ::HK_ENTITY_ACTIVATION_DO_ACTIVATE);
                hk->addWorldPostSimulationListener(&TheListener());
            }
            hull.world->IncRefCount();
            {
                std::scoped_lock lock(s.lock);
                s.ctrl = &ctrl, s.world = hull.world, s.hk = hk, s.body = body;
                s.settings = std::move(set);
                s.state = {}, s.ground = {}, s.jumped = false;
            }
            if (LogBudget())
                LOG_INFO("dynamic mover: body made - radius {:.3f}, capsule from {:.3f} to {:.3f} above the feet, float {:.3f}, mass {:.0f}",
                         hull.radius, s.settings.floatHeight, s.settings.height, s.settings.floatHeight, s.settings.mass);
            return true;
        }

        /// @note Priority::Early: inside the pogo's subscriber (First is outermost). The body goes as soon as a step's
        ///       Simulate did not claim it (vanilla's states, a synced clip, ragdoll, the flag off).
        bool OnIntegrate(Integrate::Next next, ::bhkCharacterController* self)
        {
            Slot& s = S();
            if (self && s.ctrl == self && !s.claimed.exchange(false, std::memory_order_acq_rel)) Release(s, "the ground states let go");
            return next(self);
        }

        [[maybe_unused]] const bool s_integrate =
            Integrate::Subscribe(&OnIntegrate, platform::hooks::Priority::Early, "CharDynamicMover", "char_dynamic_mover");

    }  // namespace

    bool On()
    {
        static const bool on = core::debug::FlagOn("char_dynamic_mover");
        return on;
    }

    bool Step(::bhkCharacterController& ctrl, const Hull& hull, const In& in, Out& out)
    {
        if (!hull.valid || !hull.world) return false;
        Slot& s = S();
        if (s.body && (s.ctrl != &ctrl || s.world != hull.world)) Release(s, "the controller or its world changed");
        /// @note The proxy rides at the body's feet, so a proxy far from them was moved by the game (a door, fast travel,
        ///       MoveTo within one worldspace): the body is made again where the proxy now is.
        if (s.body) {
            float bx, by, bz;
            {
                const ::bhkWorldLock wl(s.hk, 0);
                std::scoped_lock     lock(s.lock);
                const ::hkVector4& p = s.body->getPosition();
                bx = p(0), by = p(1), bz = p(2) - s.settings.HalfSegment() - s.settings.RestLength();
            }
            const float dx = hull.cx - bx, dy = hull.cy - by, dz = hull.feetZ - bz;
            if (dx * dx + dy * dy + dz * dz > kTeleport * kTeleport) Release(s, "the game moved the player");
        }
        if (!s.body && !Create(s, ctrl, hull)) return false;
        s.claimed.store(true, std::memory_order_release);

        float x, y, originZ, vx, vy;
        lc::DynamicMoverSettings settings;
        lc::MoverState           state;
        {
            const ::bhkWorldLock wl(s.hk, 0);
            std::scoped_lock     lock(s.lock);
            const ::hkVector4& p = s.body->getPosition();
            const ::hkVector4& v = s.body->getLinearVelocity();
            const float        half = s.settings.HalfSegment();
            x = p(0), y = p(1), originZ = p(2) - half;
            vx = v(0), vy = v(1);
            out.vx = v(0), out.vy = v(1), out.vz = v(2);
            out.capsuleA[0] = out.capsuleB[0] = p(0);
            out.capsuleA[1] = out.capsuleB[1] = p(1);
            out.capsuleA[2] = p(2) - half, out.capsuleB[2] = p(2) + half;
            out.radius = s.settings.radius;
            s.settings.gravity   = in.gravity;
            s.settings.jumpSpeed = in.jumpSpeed;
            s.settings.walkableZ = in.walkableZ;
            settings = s.settings;
            state    = s.state;
        }

        /// @note The probes, here where vanilla queries the world; the listener reads the floor they chose.
        const platform::physics::GameWorld game(hull.world);
        const lc::MoverGround ground = lc::ProbeMoverGround(game, settings, {}, state, x, y, originZ, vx, vy, &out.probes);
        {
            std::scoped_lock lock(s.lock);
            s.ground         = ground;
            s.state.headingX = state.headingX, s.state.headingY = state.headingY;
            s.dx = in.dx, s.dy = in.dy;
            if (in.jump) s.state.jumpWanted = true;
            out.onGround = s.state.onGround, out.walkable = s.state.walkable, out.jumping = s.state.jumping;
            out.jumped   = s.jumped;
            s.jumped     = false;
            out.pogoLength = s.state.pogoLength;
        }
        out.nx = ground.nx, out.ny = ground.ny, out.nz = ground.nz;
        out.feetX = x, out.feetY = y, out.feetZ = originZ - settings.RestLength();

        /// @note Diagnostics, once a second: whether Havok steps the body and calls the listener, and what the mover saw.
        {
            const double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
            const ::bhkWorldLock wl(s.hk, 0);
            std::scoped_lock     lock(s.lock);
            if (s.body && now - s.reportAt >= 1.0) {
                s.reportAt = now;
                int probeHits = 0;
                for (int i = 0; i < out.probes.count; i++) probeHits += out.probes.probe[i].hit ? 1 : 0;
                const ::hkVector4& v = s.body->getLinearVelocity();
                const ::hkVector4& g = s.hk->getGravity();
                LOG_INFO("dynamic mover: feet ({:.2f}, {:.2f}, {:.2f}) v ({:.2f}, {:.2f}, {:.2f}) in its world {} island {} motion {} | listener "
                         "{} calls (dt {:.4f}, {} without time, {} for another world) | gravity ({:.2f}, {:.2f}, {:.2f}) | ground {} z {:.2f} "
                         "walkable {} probes {}/{} | on ground {} walkable {} jumping {} | request ({:.2f}, {:.2f})",
                         out.feetX, out.feetY, out.feetZ, v(0), v(1), v(2), s.body->getWorld() == s.hk, s.body->getSimulationIsland() != nullptr,
                         static_cast<int>(s.body->getMotionType()), s.calls, s.lastH, s.noTime, s.otherWorld, g(0), g(1), g(2), ground.hit,
                         ground.z, ground.walkable, probeHits, out.probes.count, s.state.onGround, s.state.walkable, s.state.jumping, s.dx, s.dy);
                s.calls = s.otherWorld = s.noTime = 0;
            }
        }

        /// @note The passenger proxy: at the body's feet, asked for no motion of its own.
        ::hkVector4 feet;
        feet.set(out.feetX, out.feetY, out.feetZ, 0.0f);
        ctrl.SetPositionImpl(feet, true, false);
        ctrl.m_outVelocity.setZero4();
        return true;
    }

    bool BodyCapsule(const ::bhkCharacterController& ctrl, float a[3], float b[3], float& radius)
    {
        Slot& s = S();
        if (s.ctrl != &ctrl) return false;
        const ::bhkWorldLock wl(s.hk, 0);
        std::scoped_lock     lock(s.lock);
        if (!s.body) return false;
        const ::hkVector4& p    = s.body->getPosition();
        const float        half = s.settings.HalfSegment();
        a[0] = b[0] = p(0), a[1] = b[1] = p(1);
        a[2] = p(2) - half, b[2] = p(2) + half;
        radius = s.settings.radius;
        return true;
    }

}  // namespace CB::feature::controller::dynamic
