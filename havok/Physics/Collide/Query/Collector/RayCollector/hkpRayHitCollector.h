/// @file
/// @brief hkpRayHitCollector: receives the hits of a ray cast.
#pragma once

#include <Physics/Collide/Query/CastUtil/hkpWorldRayCastOutput.h>

namespace CB { namespace havok
{
    class hkpCdBody;

    /// @note 0x10 bytes. Slot order per the Havok SDK (unconfirmed: nothing matched calls through it).
    class hkpRayHitCollector
    {
    public:
        hkReal m_earlyOutHitFraction;   ///< +0x08 — hits beyond this fraction are ignored

        HK_FORCE_INLINE hkpRayHitCollector() { reset(); }
        HK_FORCE_INLINE void reset() { m_earlyOutHitFraction = 1.0f; }

        virtual void addRayHit(const hkpCdBody& cdBody, const hkpShapeRayCastCollectorOutput& hitInfo) = 0;   ///< slot 0
        virtual ~hkpRayHitCollector() {}                                                                      ///< slot 1
    };
} }
