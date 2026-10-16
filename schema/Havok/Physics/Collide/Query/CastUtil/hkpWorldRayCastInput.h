/// @file
/// @brief hkpWorldRayCastInput: a ray cast against the whole world.
#pragma once

#include <Common/Base/hkBase.h>

namespace CB { namespace schema
{
    /// @note 0x30 bytes. Only the fields vanilla writes are initialized (hkbDefaultRaycastInterface::castRay).
    struct hkpWorldRayCastInput
    {
        hkVector4 m_from;                          ///< +0x00
        hkVector4 m_to;                            ///< +0x10
        hkBool    m_enableShapeCollectionFilter;   ///< +0x20
        hkUint32  m_filterInfo;                    ///< +0x24 — the ray's collision filter info

        HK_FORCE_INLINE hkpWorldRayCastInput() : m_enableShapeCollectionFilter(false) {}
    };
} }
