#pragma once

/// @file
/// @brief Layouts seeded from Havok's own reflection in the exe (tools/cbmatch --seed-headers): Havok's member names,
///        the gaps as explicit padding; tools/cbmatch --check-layout proves every named member.

#include <Common/Base/hkBase.h>
#include <Physics/Collide/Shape/Compound/Collection/hkpShapeCollection.h>

namespace CB { namespace havok
{
    class hkpSampledHeightFieldShape;

    /// @brief Seeded from Havok's reflection (size 0x60).
    class hkpTriSampledHeightFieldCollection : public hkpShapeCollection
    {
    public:
        hkpSampledHeightFieldShape* m_heightfield;   ///< +0x30
        hkInt32 m_childSize;   ///< +0x38, not serialized
        hkReal m_radius;   ///< +0x3c
        hkArray<hkUint16> m_weldingInfo;   ///< +0x40
        hkVector4 m_triangleExtrusion;   ///< +0x50
    };
} }
