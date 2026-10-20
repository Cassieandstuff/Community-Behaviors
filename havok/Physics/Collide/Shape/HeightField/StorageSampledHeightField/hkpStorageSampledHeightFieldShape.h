#pragma once

/// @file
/// @brief Layouts seeded from Havok's own reflection in the exe (tools/cbmatch --seed-headers): Havok's member names,
///        the gaps as explicit padding; tools/cbmatch --check-layout proves every named member.

#include <Common/Base/hkBase.h>
#include <Physics/Collide/Shape/HeightField/SampledHeightField/hkpSampledHeightFieldShape.h>

namespace CB { namespace havok
{
    /// @brief Seeded from Havok's reflection (size 0x90).
    class hkpStorageSampledHeightFieldShape : public hkpSampledHeightFieldShape
    {
    public:
        hkArray<hkReal> m_storage;   ///< +0x70
        hkBool m_triangleFlip;   ///< +0x80
    };
} }
