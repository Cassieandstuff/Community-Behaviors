#pragma once

/// @file
/// @brief Layouts seeded from Havok's own reflection in the exe (tools/cbmatch --seed-headers): Havok's member names,
///        the gaps as explicit padding; tools/cbmatch --check-layout proves every named member.

#include <Common/Base/hkBase.h>
#include <Physics/Collide/Shape/HeightField/hkpHeightFieldShape.h>

namespace CB { namespace schema
{
    /// @brief Seeded from Havok's reflection (size 0x50).
    class hkpPlaneShape : public hkpHeightFieldShape
    {
    public:
        hkVector4 m_plane;   ///< +0x20
        hkVector4 m_aabbCenter;   ///< +0x30
        hkVector4 m_aabbHalfExtents;   ///< +0x40
    };
} }
