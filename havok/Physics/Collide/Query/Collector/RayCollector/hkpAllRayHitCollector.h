/// @file
/// @brief hkpAllRayHitCollector: keeps every hit of a ray cast (eight in place before it allocates).
#pragma once

#include <Physics/Collide/Query/Collector/RayCollector/hkpRayHitCollector.h>

namespace CB { namespace havok
{
    /// @note 0x320 bytes; vtable 0x1419e3778.
    class hkpAllRayHitCollector : public hkpRayHitCollector
    {
    public:
        hkInplaceArray<hkpWorldRayCastOutput, 8> m_hits;   ///< +0x10

        HK_FORCE_INLINE hkpAllRayHitCollector() {}
        virtual void addRayHit(const hkpCdBody& cdBody, const hkpShapeRayCastCollectorOutput& hitInfo);

        HK_FORCE_INLINE const hkArray<hkpWorldRayCastOutput>& getHits() const { return m_hits; }
    };
} }
