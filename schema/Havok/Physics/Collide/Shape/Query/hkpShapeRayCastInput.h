/// @file
/// @brief hkpShapeRayCastInput: a ray cast against one shape, in the shape's space.
#pragma once

#include <Common/Base/hkBase.h>

namespace CB { namespace schema
{
    struct hkpRayShapeCollectionFilter;

    /// @note 0x30 bytes.
    struct hkpShapeRayCastInput
    {
        hkVector4                          m_from;                        ///< +0x00
        hkVector4                          m_to;                          ///< +0x10
        hkUint32                           m_filterInfo;                  ///< +0x20
        const hkpRayShapeCollectionFilter* m_rayShapeCollectionFilter;   ///< +0x28

        HK_FORCE_INLINE hkpShapeRayCastInput() : m_filterInfo(0), m_rayShapeCollectionFilter(HK_NULL) {}
    };
} }
