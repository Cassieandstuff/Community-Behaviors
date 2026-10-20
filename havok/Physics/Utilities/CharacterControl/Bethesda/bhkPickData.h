/// @file
/// @brief bhkPickData: Bethesda's ray-cast query against a bhkWorld (bhkWorld::PickObject).
/// @note Not Havok: declared only as far as the character controller's code uses it.
#pragma once

#include <Physics/Collide/Query/CastUtil/hkpWorldRayCastInput.h>
#include <Physics/Collide/Query/CastUtil/hkpWorldRayCastOutput.h>

namespace CB { namespace havok
{
    class hkpClosestRayHitCollector;
    class hkpAllCdPointCollector;
    class hkpRayHitCollector;

    /// @note 0xd0 bytes. Members past the output per CommonLibSSE (the constructor clears them; their use unproven).
    struct bhkPickData
    {
        hkpWorldRayCastInput       m_rayInput;            ///< +0x00 — the ray, in Havok units
        hkpWorldRayCastOutput      m_rayOutput;           ///< +0x30 — the closest hit
        hkVector4                  m_ray;                 ///< +0x90
        hkpClosestRayHitCollector* m_rayHitCollectorA0;   ///< +0xa0
        hkpAllCdPointCollector*    m_rayHitCollectorA8;   ///< +0xa8
        hkpRayHitCollector*        m_rayHitCollectorB0;   ///< +0xb0
        hkpRayHitCollector*        m_rayHitCollectorB8;   ///< +0xb8
        bool                       m_unkC0;               ///< +0xc0
        hkUint8                    m_padC1[0xd0 - 0xc1];

        HK_FORCE_INLINE bhkPickData()
        :   m_ray(hkVector4::getZero()),
            m_rayHitCollectorA0(HK_NULL),
            m_rayHitCollectorA8(HK_NULL),
            m_rayHitCollectorB0(HK_NULL),
            m_rayHitCollectorB8(HK_NULL),
            m_unkC0(false)
        {
        }
    };
} }
