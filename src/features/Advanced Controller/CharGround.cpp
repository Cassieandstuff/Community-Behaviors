#include "PCH.h"

#include "CharGround.h"

#include <Common/Base/Types/Geometry/Aabb/hkAabb.h>
#include <Physics/Dynamics/Phantom/hkpCachingShapePhantom.h>
#include <Physics/Utilities/CharacterControl/Bethesda/bhkPickData.h>
#include <Physics/Utilities/CharacterControl/StateMachine/Util/hkpCharacterMovementUtil.h>

#include <algorithm>
#include <core/debug/Settings.h>

#include <cstdint>
#include <cstdlib>
#include <mutex>
#include <string>

namespace CB::feature::controller {

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
            FloorHit         h;
            hk::bhkPickData  pick;
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

    void FloorRayXY(const Hull& hull, int i, const FloorTunables& t, float& x, float& y)
    {
        const float perch = hull.radius * t.perchFraction;
        const float a     = 6.2831853f * static_cast<float>(i - 1) / 8.0f;
        x                 = hull.cx + (i == 0 ? 0.0f : perch * std::cos(a));
        y                 = hull.cy + (i == 0 ? 0.0f : perch * std::sin(a));
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
        const float       probe  = ctrl.m_stepHeight;
        const hk::CFilter filter = ctrl.GetCollisionFilterInfo();
        for (int i = 0; i < static_cast<int>(v.hits.size()); i++) {
            float x, y;
            FloorRayXY(hull, i, t, x, y);
            v.hits[i] = CastDown(hull.world, filter.m_filter, x, y, hull.feetZ + hull.radius, hull.feetZ - probe, hull.feetZ);
        }
        float sx = 0, sy = 0, sz = 0;
        v.stickDz = -1.0e9f;
        for (const FloorHit& h : v.hits) {
            if (!h.hit || h.layer == kCharControllerLayer || h.nz < t.wallNormalZ) continue;
            const bool walkable = h.nz >= ctrl.m_zNormal;
            if (h.dz >= -t.footTolerance) {
                ++v.supportHits;
                sx += h.nx, sy += h.ny, sz += h.nz;
                if (walkable) {
                    ++v.walkableHits;
                    if (!v.supportBody) v.supportBody = h.body;
                }
            } else if (walkable) {
                v.stickable = true;
                v.stickDz   = (std::max)(v.stickDz, h.dz);
            }
        }
        /// @note Walkability is judged on the AVERAGED normal of every hit under the feet, not on any single ray: the
        ///       first ground-states capture flipped Grounded / Sliding 172 times (117 within a second) on rough 50-60 deg
        ///       terrain because one walkable ray among four to seven steep ones decided it.
        const float len = std::sqrt(sx * sx + sy * sy + sz * sz);
        if (len > 1.0e-6f) v.groundNx = sx / len, v.groundNy = sy / len, v.groundNz = sz / len;
        v.ground = v.supportHits == 0 ? Ground::Airborne : v.groundNz >= ctrl.m_zNormal ? Ground::Grounded : Ground::Sliding;
        if (v.ground == Ground::Grounded && !v.supportBody) {
            for (const FloorHit& h : v.hits)
                if (h.hit && h.layer != kCharControllerLayer && h.nz >= t.wallNormalZ && h.dz >= -t.footTolerance) {
                    v.supportBody = h.body;
                    break;
                }
        }
        if (!v.stickable) v.stickDz = 0.0f;
        return v;
    }

    namespace {

        /// @brief calculateMovement's output for a request of `forward` along the tangent and `side` along the side.
        void Steer(const hk::bhkCharacterController& c, float forward, float side, float& dx, float& dy)
        {
            alignas(16) std::array<std::uint8_t, sizeof(hk::hkpMovementUtilInput)> storage{};
            auto& in = *reinterpret_cast<hk::hkpMovementUtilInput*>(storage.data());
            in.m_forward = c.m_forward;
            in.m_up.set(0.0f, 0.0f, 1.0f, 0.0f);
            in.m_surfaceNormal.set(0.0f, 0.0f, 1.0f, 0.0f);
            in.m_desiredVelocity.set(forward, side, 0.0f, 0.0f);
            in.m_gain             = 1.0f;
            in.m_maxVelocityDelta = 1.0e6f;
            hk::hkVector4 out;
            out.set(0.0f, 0.0f, 0.0f, 0.0f);
            hk::hkpCharacterMovementUtil::calculateMovement(in, out);
            dx = out(0), dy = out(1);
        }

    }  // namespace

    void DesiredHorizontal(const hk::bhkCharacterController& ctrl, float& dx, float& dy)
    {
        Steer(ctrl, ctrl.m_velocityMod(1), ctrl.m_velocityMod(0), dx, dy);
    }

    bool MovementAxes(const hk::bhkCharacterController& ctrl, float& forwardX, float& forwardY, float& sideX, float& sideY)
    {
        Steer(ctrl, 1.0f, 0.0f, forwardX, forwardY);
        Steer(ctrl, 0.0f, 1.0f, sideX, sideY);
        const float f = forwardX * forwardX + forwardY * forwardY, s = sideX * sideX + sideY * sideY;
        if (f < 1.0e-6f || s < 1.0e-6f) return false;
        const float fi = 1.0f / std::sqrt(f), si = 1.0f / std::sqrt(s);
        forwardX *= fi, forwardY *= fi, sideX *= si, sideY *= si;
        return true;
    }

    float SettingFloat(std::string_view key, float def)
    {
        const std::string s   = core::debug::Value("Character", key);
        char*             end = nullptr;
        const float       f   = s.empty() ? def : std::strtof(s.c_str(), &end);
        return s.empty() || end == s.c_str() ? def : f;
    }

    float IniFloat(const char* name, float def)
    {
        auto*        ini = RE::INISettingCollection::GetSingleton();
        RE::Setting* s   = ini ? ini->GetSetting(name) : nullptr;
        return s ? s->GetFloat() : def;
    }

    namespace {
        std::mutex                        g_publishLock;
        const hk::bhkCharacterController* g_publishCtrl = nullptr;
        FloorVerdict                      g_published;
    }  // namespace

    void PublishVerdict(const hk::bhkCharacterController& ctrl, const FloorVerdict& v)
    {
        std::scoped_lock lock(g_publishLock);
        g_publishCtrl = &ctrl;
        g_published   = v;
    }

    bool PublishedVerdict(const hk::bhkCharacterController& ctrl, FloorVerdict& out)
    {
        std::scoped_lock lock(g_publishLock);
        if (g_publishCtrl != &ctrl) return false;
        out = g_published;
        return true;
    }

}  // namespace CB::feature::controller
