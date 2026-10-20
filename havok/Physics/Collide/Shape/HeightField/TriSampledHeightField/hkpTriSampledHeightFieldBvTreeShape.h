#pragma once

/// @file
/// @brief Layouts seeded from Havok's own reflection in the exe (tools/cbmatch --seed-headers): Havok's member names,
///        the gaps as explicit padding; tools/cbmatch --check-layout proves every named member.

#include <Common/Base/hkBase.h>
#include <Physics/Collide/Shape/Compound/Tree/hkpBvTreeShape.h>
#include <Physics/Collide/Shape/hkpShapeContainer.h>

namespace CB { namespace havok
{
    /// @brief Seeded from Havok's reflection (size 0x50).
    class hkpTriSampledHeightFieldBvTreeShape : public hkpBvTreeShape
    {
    public:
        hkpSingleShapeContainer m_childContainer;   ///< +0x28
        hkInt32 m_childSize;   ///< +0x38, not serialized
        hkBool m_wantAabbRejectionTest;   ///< +0x3c
        hkUint8 m_padding[12];   ///< +0x3d
    };
} }
