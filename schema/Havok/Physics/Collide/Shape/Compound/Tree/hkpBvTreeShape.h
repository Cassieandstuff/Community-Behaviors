#pragma once

/// @file
/// @brief Layouts seeded from Havok's own reflection in the exe (tools/cbmatch --seed-headers): Havok's member names,
///        the gaps as explicit padding; tools/cbmatch --check-layout proves every named member.

#include <Common/Base/hkBase.h>
#include <Physics/Collide/Shape/hkpShape.h>

namespace CB { namespace schema
{
    /// @brief Seeded from Havok's reflection (size 0x28).
    class hkpBvTreeShape : public hkpShape
    {
    public:
        enum BvTreeType
        {
            BVTREE_MOPP = 0,
            BVTREE_TRISAMPLED_HEIGHTFIELD = 1,
            BVTREE_USER = 2,
            BVTREE_MAX = 3,
        };
        hkUint8 m_bvTreeType;   ///< +0x20
    };
} }
