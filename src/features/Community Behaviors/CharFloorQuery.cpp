#include "PCH.h"

#include "CharSweep.h"

#include <core/hooks/takeover/Override.h>
#include <feature-sdk/havok/Physics/Utilities/CharacterControl/Bethesda/bhkCharProxyController.h>

#include <Common/Base/Types/Geometry/Aabb/hkAabb.h>
#include <Physics/Dynamics/Entity/hkpRigidBody.h>
#include <Physics/Dynamics/Phantom/hkpCachingShapePhantom.h>
#include <Physics/Utilities/CharacterControl/Bethesda/bhkPickData.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <mutex>

/// @file
/// @brief Step 2 of the character controller spec, observe-only: the M4 floor query as a ray footprint (one ray at the
///        hull's axis, a ring of eight at the perch radius), run for the player right after vanilla's support check and
///        logged against vanilla's own support verdict. Nothing it computes is written back.
/// @note Rays go through bhkWorld::PickObject, the call vanilla OnGround::Simulate makes from the same point in the
///       step (inside UpdatePhysics, before the state simulates), with the controller's own collision filter.
/// @note Feet and radius come from the live hull's world AABB (shape vtable slot 7, getAabb), so DCA's reshapes are seen.

namespace CB::feature {

    namespace {

        namespace hk = CB::schema;
        using CheckSupport = core::hooks::Override<&hk::bhkCharProxyController::CheckSupportImpl>;

        constexpr hk::hkUint32 kSupport                     = 1u << 8;
        constexpr hk::hkUint32 kHasPotentialSupportManifold = 1u << 9;
        constexpr unsigned     kCharControllerLayer         = 30;
        constexpr int          kRing                        = 8;

        /// @brief Starting tunables (Havok units unless noted); the scenarios set them (spec M4).
        /// @note The rays start one hull radius above the feet so the uphill ring rays begin above a walkable slope.
        constexpr float kPerchFraction = 0.6f;   ///< perch radius as a fraction of the hull radius
        constexpr float kFootTolerance = 0.1f;   ///< about 7 game units
        /// @brief Faces steeper than about 72 deg are walls, never ground candidates.
        /// @note Found in the first capture: a ring ray at the perch radius grazes the side face of a lip or wall beside
        ///       the feet (nz about 0.11) and would count a wall as sliding ground; walls belong to the step query.
        constexpr float kWallNormalZ = 0.3f;

        enum class Ground { Airborne, Grounded, Sliding };
        const char* Name(Ground g) { return g == Ground::Grounded ? "Grounded" : g == Ground::Sliding ? "Sliding" : "Airborne"; }

        struct Hit {
            bool                 hit = false;
            float                dz  = 0;   ///< hit height above the feet
            float                nx = 0, ny = 0, nz = 0;
            unsigned             layer = 0;
            const hk::hkpCollidable* body = nullptr;
        };

        struct Verdict {
            Ground ground    = Ground::Airborne;
            bool   stickable = false;   ///< a walkable hit within probe depth below the feet (M8)
            int    supportHits = 0, walkableHits = 0;
        };

        bool IsPlayer(const hk::bhkCharacterController* ctrl)
        {
            auto*       player = RE::PlayerCharacter::GetSingleton();
            const auto* own    = player ? player->GetCharController() : nullptr;
            return own && static_cast<const void*>(ctrl) == static_cast<const void*>(own);
        }

        /// @brief Whether `ctrl` is the bhkCharacterController base of a bhkCharProxyController.
        bool IsProxy(const hk::bhkCharacterController& ctrl)
        {
            static const std::uintptr_t vtable = REL::Relocation<std::uintptr_t>(RE::VTABLE_bhkCharProxyController[1]).address();
            return *reinterpret_cast<const std::uintptr_t*>(&ctrl) == vtable;
        }

        /// @brief hkpShape::getAabb, vtable slot 7 (the schema still calls it unk_07).
        /// @note Slot proven by the exe: bhkCharacterControllerCinfo (0x140e9cdf0) calls a shape's +0x38 with (identity
        ///       transform, tolerance 0, out AABB) and reads min/max back.
        void GetAabb(const hk::hkpShape* shape, const hk::hkTransform& transform, hk::hkAabb& out)
        {
            using Fn = void (*)(const hk::hkpShape*, const hk::hkTransform&, float, hk::hkAabb&);
            const auto fn = reinterpret_cast<Fn>((*reinterpret_cast<void* const* const*>(shape))[7]);
            fn(shape, transform, 0.0f, out);
        }

        Hit CastDown(hk::bhkWorld* world, hk::hkUint32 filter, float x, float y, float fromZ, float toZ, float feetZ)
        {
            Hit h;
            /// @note Zeroed storage plus the output's reset is what bhkPickData's constructor leaves; the constructor itself
            ///       reads vanilla's g_vectorConstantZero, which the plugin does not link.
            alignas(16) std::array<std::uint8_t, sizeof(hk::bhkPickData)> storage{};
            auto& pick = *reinterpret_cast<hk::bhkPickData*>(storage.data());
            pick.m_rayOutput.reset();
            pick.m_rayInput.m_from.set(x, y, fromZ, 0.0f);
            pick.m_rayInput.m_to.set(x, y, toZ, 0.0f);
            pick.m_rayInput.m_filterInfo = filter;
            world->PickObject(pick);
            if (!pick.m_rayOutput.hasHit() || !pick.m_rayOutput.m_rootCollidable) return h;
            h.hit   = true;
            h.dz    = fromZ + (toZ - fromZ) * pick.m_rayOutput.m_hitFraction - feetZ;
            h.nx    = pick.m_rayOutput.m_normal(0);
            h.ny    = pick.m_rayOutput.m_normal(1);
            h.nz    = pick.m_rayOutput.m_normal(2);
            h.body  = pick.m_rayOutput.m_rootCollidable;
            h.layer = h.body->getCollisionFilterInfo() & 0x7f;
            return h;
        }

        using GetVelocity = havok::bhkCharProxyController::GetLinearVelocityImpl;

        /// @brief Step 2b's observe check of the sweep helper: the hull swept a quarter second ahead along its horizontal
        ///        velocity, logged whenever its first hit changes.
        constexpr float kSweepSeconds = 0.25f;
        int             g_lastSweepKey = -1;
        std::uint32_t   g_sweepLogged  = 0;

        void ObserveSweep(hk::bhkCharacterController* self, hk::bhkWorld* world, const hk::hkpCollidable* coll)
        {
            hk::hkVector4 v;
            GetVelocity::Call(self, v);
            const float vx = v(0) * kSweepSeconds, vy = v(1) * kSweepSeconds;
            if (vx * vx + vy * vy < 1.0e-4f) return;
            const hk::hkVector4& from = coll->getTransform().getTranslation();
            const hk::hkVector4  to(from(0) + vx, from(1) + vy, from(2), 0.0f);
            const character::SweepHit s = character::Sweep(world, coll, to, 0.0f, self->m_zNormal);
            const int key = s.hit ? 1 + static_cast<int>(s.fraction * 10.0f) * 4 + (s.contact.m_separatingNormal(2) > 0.3f ? 2 : 0) : 0;
            if (key == g_lastSweepKey || g_sweepLogged >= 2000) return;
            g_lastSweepKey = key;
            ++g_sweepLogged;
            if (!s.hit) LOG_INFO("sweep #{}: {:.3f} ahead, clear", g_sweepLogged, std::sqrt(vx * vx + vy * vy));
            else
                LOG_INFO("sweep #{}: {:.3f} ahead, hit at fraction {:.3f} n=({:.2f},{:.2f},{:.2f}) layer {}", g_sweepLogged,
                         std::sqrt(vx * vx + vy * vy), s.fraction, s.contact.m_separatingNormal(0), s.contact.m_separatingNormal(1),
                         s.contact.m_separatingNormal(2), s.body ? (s.body->getCollisionFilterInfo() & 0x7f) : 0u);
        }

        std::mutex             g_lock;
        int                    g_lastKey = -1;
        std::uint32_t          g_logged  = 0;
        std::uint64_t          g_runs = 0, g_agree = 0;
        constexpr std::uint32_t kMaxLogged = 4000;

        void OnCheckSupport(CheckSupport::Next next, hk::bhkCharacterController* self)
        {
            next(self);
            hk::bhkCharacterController& ctrl = *self;
            if (!IsPlayer(&ctrl) || !IsProxy(ctrl)) return;
            auto&               proxyCtrl = static_cast<hk::bhkCharProxyController&>(ctrl);
            hk::bhkWorld*       world     = ctrl.GetHavokWorld();
            hk::bhkCachingShapePhantom* bp = proxyCtrl.m_shapePhantom.get();
            hk::hkpCachingShapePhantom* phantom = bp ? bp->GetPhantom() : nullptr;
            if (!world || !phantom) return;
            const hk::hkpCollidable* coll  = phantom->getCollidable();
            const hk::hkpShape*      shape = coll->getShape();
            if (!shape) return;

            hk::hkAabb aabb;
            GetAabb(shape, coll->getTransform(), aabb);
            const float feetZ  = aabb.m_min(2);
            const float cx     = (aabb.m_min(0) + aabb.m_max(0)) * 0.5f;
            const float cy     = (aabb.m_min(1) + aabb.m_max(1)) * 0.5f;
            const float radius = (std::min)(aabb.m_max(0) - aabb.m_min(0), aabb.m_max(1) - aabb.m_min(1)) * 0.5f;
            const float perch  = radius * kPerchFraction;
            const float probe  = ctrl.m_stepHeight;
            const hk::CFilter filter = ctrl.GetCollisionFilterInfo();

            std::array<Hit, kRing + 1> hits;
            for (int i = 0; i <= kRing; i++) {
                const float a  = 6.2831853f * static_cast<float>(i - 1) / kRing;
                const float ox = i == 0 ? 0.0f : perch * std::cos(a);
                const float oy = i == 0 ? 0.0f : perch * std::sin(a);
                hits[i] = CastDown(world, filter.m_filter, cx + ox, cy + oy, feetZ + radius, feetZ - probe, feetZ);
            }

            Verdict v;
            for (const Hit& h : hits) {
                if (!h.hit || h.layer == kCharControllerLayer || h.nz < kWallNormalZ) continue;
                const bool walkable = h.nz >= ctrl.m_zNormal;
                if (h.dz >= -kFootTolerance) {
                    ++v.supportHits;
                    if (walkable) ++v.walkableHits;
                } else if (walkable) v.stickable = true;
            }
            v.ground = v.walkableHits ? Ground::Grounded : v.supportHits ? Ground::Sliding : Ground::Airborne;

            const bool vanilla = (ctrl.m_surfaceInfo.m_supportedState == hk::hkpSurfaceInfo::SUPPORTED && !ctrl.m_onlyWallContacts)
                              || (ctrl.m_flags & kHasPotentialSupportManifold);
            const bool ours    = v.ground == Ground::Grounded;

            std::scoped_lock lock(g_lock);
            ObserveSweep(self, world, coll);
            ++g_runs;
            if (ours == vanilla) ++g_agree;
            if ((g_runs & (g_runs - 1)) == 0)
                LOG_INFO("floor query: {} runs, {} agree with vanilla support ({:.1f}%); hull feet {:.3f} radius {:.3f} perch {:.3f} probe {:.3f}",
                         g_runs, g_agree, 100.0 * g_agree / g_runs, feetZ, radius, perch, probe);
            const int key = static_cast<int>(v.ground) * 8 + (vanilla ? 4 : 0) + (v.stickable ? 2 : 0) + ((ctrl.m_flags & kSupport) ? 1 : 0);
            if (key == g_lastKey || g_logged >= kMaxLogged) return;
            g_lastKey = key;
            ++g_logged;
            LOG_INFO("floor query #{}: ours {} (support hits {}, walkable {}, stickable {}) vanilla {} (surface {} onlyWalls {} bit9 {}) state {}{}",
                     g_logged, Name(v.ground), v.supportHits, v.walkableHits, v.stickable, vanilla ? "supported" : "unsupported",
                     static_cast<int>(ctrl.m_surfaceInfo.m_supportedState), ctrl.m_onlyWallContacts,
                     (ctrl.m_flags & kHasPotentialSupportManifold) != 0, static_cast<int>(ctrl.m_context.m_currentState),
                     ours == vanilla ? "" : "  <- DISAGREE");
            if (ours != vanilla) {
                for (int i = 0; i <= kRing; i++) {
                    const Hit& h = hits[i];
                    if (!h.hit) LOG_INFO("floor query   ray {}: no hit", i);
                    else
                        LOG_INFO("floor query   ray {}: dz {:.3f} n=({:.2f},{:.2f},{:.2f}) layer {} body {:#x}", i, h.dz, h.nx, h.ny, h.nz, h.layer,
                                 reinterpret_cast<std::uintptr_t>(h.body));
                }
            }
        }

        [[maybe_unused]] const bool s_subscribed =
            CheckSupport::Subscribe(&OnCheckSupport, core::hooks::Priority::Last, "CharFloorQuery", "char_floor_query");

    }  // namespace

}  // namespace CB::feature
