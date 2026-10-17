#pragma once

/// @file
/// @brief Layouts seeded from Havok's own reflection in the exe (tools/cbmatch --seed-headers): Havok's member names,
///        the gaps as explicit padding; tools/cbmatch --check-layout proves every named member.

#include <Common/Base/hkBase.h>
#include <Physics/Collide/Shape/HeightField/hkpHeightFieldShape.h>

namespace CB { namespace schema
{
    /// @brief Seeded from Havok's reflection (size 0x70).
    class hkpSampledHeightFieldShape : public hkpHeightFieldShape
    {
    public:
        enum HeightFieldType
        {
            HEIGHTFIELD_STORAGE = 0,
            HEIGHTFIELD_COMPRESSED = 1,
            HEIGHTFIELD_USER = 2,
            HEIGHTFIELD_MAX_ID = 3,
        };
        hkInt32 m_xRes;   ///< +0x20
        hkInt32 m_zRes;   ///< +0x24
        hkReal m_heightCenter;   ///< +0x28
        hkBool m_useProjectionBasedHeight;   ///< +0x2c
        hkUint8 m_heightfieldType;   ///< +0x2d
        hkVector4 m_intToFloatScale;   ///< +0x30
        hkVector4 m_floatToIntScale;   ///< +0x40
        hkVector4 m_floatToIntOffsetFloorCorrected;   ///< +0x50
        hkVector4 m_extents;   ///< +0x60
    };
} }
