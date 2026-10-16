/// @file
/// @brief hkbDefaultRaycastInterface, reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/Utils/hkbDefaultRaycastInterface.h>

#include <Physics/Collide/Agent/Collidable/hkpCollidable.h>
#include <Physics/Collide/Query/CastUtil/hkpWorldRayCastInput.h>
#include <Physics/Collide/Query/Collector/RayCollector/hkpClosestRayHitCollector.h>
#include <Physics/Collide/Shape/Convex/hkpConvexShape.h>
#include <Physics/Dynamics/World/hkpWorld.h>

namespace CB { namespace schema
{
    hkbDefaultRaycastInterface::hkbDefaultRaycastInterface(hkpWorld* world)
    :   m_world(world),
        m_minUpDotNormal(0.0f)
    {
    }

    void hkbDefaultRaycastInterface::setWorld(hkpWorld* world)
    {
        m_world = world;
    }

    hkBool hkbDefaultRaycastInterface::castRay(const hkVector4& fromWS, const hkVector4& toWS, hkUint32 collisionFilterInfo, hkReal& hitFractionOut,
                                               hkVector4& normalWSOut)
    {
        if (m_world == HK_NULL) return false;

        HK_TIMER_BEGIN("castRay");
        hkpWorldRayCastInput input;
        input.m_from       = fromWS;
        input.m_to         = toWS;
        input.m_filterInfo = collisionFilterInfo;
        hkpClosestRayHitCollector collector;
        m_world->castRay(input, collector);

        const bool hasHit = collector.hasHit();
        if (hasHit) {
            const hkpWorldRayCastOutput& hit = collector.getHit();
            normalWSOut                      = hit.m_normal;
            hitFractionOut                   = hit.m_hitFraction;

            if (m_minUpDotNormal > 0.0f) {
                hkVector4 up = m_world->getGravity();
                up.normalize3();
                up.mul4(hkSimdReal(-1.0f));
                if (up.dot3(normalWSOut).getReal() < m_minUpDotNormal) {
                    HK_TIMER_END();
                    return false;
                }
            }

            const hkpShape* shape = hit.m_rootCollidable->getShape();
            if (shape->getType() == HK_SHAPE_CONVEX_TRANSLATE) {
                hkVector4 ray;
                ray.setSub4(input.m_to, input.m_from);
                const hkReal rayLength = ray.length3().getReal();
                hitFractionOut         = hkMath::max2(hit.m_hitFraction - static_cast<const hkpConvexShape*>(shape)->getRadius() / rayLength, 0.0f);
            }
        }
        HK_TIMER_END();
        return hasHit;
    }
} }
