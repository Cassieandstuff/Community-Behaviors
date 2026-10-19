#include "PCH.h"

#include "CharGround.h"

#include <Common/Base/Types/Geometry/Aabb/hkAabb.h>
#include <Physics/Dynamics/Phantom/hkpCachingShapePhantom.h>
#include <Physics/Utilities/CharacterControl/Bethesda/bhkPickData.h>

#include <algorithm>
#include <cstdint>

namespace CB::feature::character {

    namespace {

        /// @brief hkpShape::getAabb, vtable slot 7 (the schema still calls it unk_07).
        /// @note Proven twice in the exe: bhkCharacterControllerCinfo (0x140e9cdf0) and hkpWorldLinearCaster
        ///       (0x140bea990) call a shape's +0x38 with (transform, tolerance, out AABB).
        void GetAabb(const hk::hkpShape* shape, const hk::hkTransform& transform, hk::hkAabb& out)
        {
            using Fn      = void (*)(const hk::hkpShape*, const hk::hkTransform&, float, hk::hkAabb&);
            const auto fn = reinterpret_cast<Fn>((*reinterpret_cast<void* const* const*>(shape))[7]);
            fn(shape, transform, 0.0f, out);
        }

        FloorHit CastDown(hk::bhkWorld* world, hk::hkUint32 filter, float x, float y, float fromZ, float toZ, float feetZ)
        {
            FloorHit h;
            /// @note Zeroed storage plus the output's reset is what bhkPickData's constructor leaves; the constructor
            ///       itself reads vanilla's g_vectorConstantZero, which the plugin does not link.
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

    }  // namespace

    bool IsPlayer(const hk::bhkCharacterController* ctrl)
    {
        auto*       player = RE::PlayerCharacter::GetSingleton();
        const auto* own    = player ? player->GetCharController() : nullptr;
        return own && static_cast<const void*>(ctrl) == static_cast<const void*>(own);
    }

    bool IsProxy(const hk::bhkCharacterController& ctrl)
    {
        static const std::uintptr_t vtable = REL::Relocation<std::uintptr_t>(RE::VTABLE_bhkCharProxyController[1]).address();
        return *reinterpret_cast<const std::uintptr_t*>(&ctrl) == vtable;
    }

    const char* Name(Ground g) { return g == Ground::Grounded ? "Grounded" : g == Ground::Sliding ? "Sliding" : "Airborne"; }

    Hull GetHull(hk::bhkCharacterController& ctrl)
    {
        Hull h;
        auto&                       proxyCtrl = static_cast<hk::bhkCharProxyController&>(ctrl);
        hk::bhkCachingShapePhantom* bp        = proxyCtrl.m_shapePhantom.get();
        hk::hkpCachingShapePhantom* phantom   = bp ? bp->GetPhantom() : nullptr;
        h.world                               = ctrl.GetHavokWorld();
        if (!h.world || !phantom) return h;
        h.collidable = phantom->getCollidable();
        const hk::hkpShape* shape = h.collidable->getShape();
        if (!shape) return h;
        hk::hkAabb aabb;
        GetAabb(shape, h.collidable->getTransform(), aabb);
        h.feetZ  = aabb.m_min(2);
        h.topZ   = aabb.m_max(2);
        h.cx     = (aabb.m_min(0) + aabb.m_max(0)) * 0.5f;
        h.cy     = (aabb.m_min(1) + aabb.m_max(1)) * 0.5f;
        h.radius = (std::min)(aabb.m_max(0) - aabb.m_min(0), aabb.m_max(1) - aabb.m_min(1)) * 0.5f;
        h.valid  = h.radius > 0.0f;
        return h;
    }

    FloorVerdict QueryFloor(hk::bhkCharacterController& ctrl, const Hull& hull, const FloorTunables& t)
    {
        return QueryFloorAt(ctrl, hull, 0.0f, 0.0f, 0.0f, t);
    }

    FloorVerdict QueryFloorAt(hk::bhkCharacterController& ctrl, const Hull& hullHere, float dx, float dy, float dz, const FloorTunables& t)
    {
        FloorVerdict v;
        if (!hullHere.valid) return v;
        Hull hull = hullHere;
        hull.cx += dx, hull.cy += dy, hull.feetZ += dz, hull.topZ += dz;
        const float       perch  = hull.radius * t.perchFraction;
        const float       probe  = ctrl.m_stepHeight;
        const hk::CFilter filter = ctrl.GetCollisionFilterInfo();
        for (int i = 0; i < static_cast<int>(v.hits.size()); i++) {
            const float a  = 6.2831853f * static_cast<float>(i - 1) / 8.0f;
            const float ox = i == 0 ? 0.0f : perch * std::cos(a);
            const float oy = i == 0 ? 0.0f : perch * std::sin(a);
            v.hits[i] = CastDown(hull.world, filter.m_filter, hull.cx + ox, hull.cy + oy, hull.feetZ + hull.radius, hull.feetZ - probe, hull.feetZ);
        }
        float sx = 0, sy = 0, sz = 0, wx = 0, wy = 0, wz = 0;
        v.stickDz = -1.0e9f;
        for (const FloorHit& h : v.hits) {
            if (!h.hit || h.layer == kCharControllerLayer || h.nz < t.wallNormalZ) continue;
            const bool walkable = h.nz >= ctrl.m_zNormal;
            if (h.dz >= -t.footTolerance) {
                ++v.supportHits;
                sx += h.nx, sy += h.ny, sz += h.nz;
                if (walkable) {
                    ++v.walkableHits;
                    wx += h.nx, wy += h.ny, wz += h.nz;
                    if (!v.supportBody) v.supportBody = h.body;
                }
            } else if (walkable) {
                v.stickable = true;
                v.stickDz   = (std::max)(v.stickDz, h.dz);
            }
        }
        v.ground = v.walkableHits ? Ground::Grounded : v.supportHits ? Ground::Sliding : Ground::Airborne;
        const float nx = v.walkableHits ? wx : sx, ny = v.walkableHits ? wy : sy, nz = v.walkableHits ? wz : sz;
        const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
        if (len > 1.0e-6f) v.groundNx = nx / len, v.groundNy = ny / len, v.groundNz = nz / len;
        if (!v.stickable) v.stickDz = 0.0f;
        return v;
    }

}  // namespace CB::feature::character
