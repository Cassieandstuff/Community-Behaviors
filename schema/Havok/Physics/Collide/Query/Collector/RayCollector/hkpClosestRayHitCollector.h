/// @file
/// @brief hkpClosestRayHitCollector: keeps the nearest hit of a ray cast.
#pragma once

#include <Physics/Collide/Query/Collector/RayCollector/hkpRayHitCollector.h>

namespace CB { namespace schema
{
    /// @note 0x70 bytes; vtable 0x1417ed6d8.
    class hkpClosestRayHitCollector : public hkpRayHitCollector
    {
    public:
        hkpWorldRayCastOutput m_rayHit;   ///< +0x10

        HK_FORCE_INLINE hkpClosestRayHitCollector() {}
        virtual void addRayHit(const hkpCdBody& cdBody, const hkpShapeRayCastCollectorOutput& hitInfo);

        HK_FORCE_INLINE bool                         hasHit() const { return m_rayHit.hasHit(); }
        HK_FORCE_INLINE const hkpWorldRayCastOutput& getHit() const { return m_rayHit; }
    };
} }
