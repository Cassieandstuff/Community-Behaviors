#pragma once

/// @file
/// @brief Layouts seeded from Havok's own reflection in the exe (tools/cbmatch --seed-headers): Havok's member names,
///        the gaps as explicit padding; tools/cbmatch --check-layout proves every named member.

#include <Common/Base/hkBase.h>
#include <Physics/Collide/Shape/hkpShape.h>
#include <Physics/Collide/Shape/hkpShapeContainer.h>

namespace CB { namespace havok
{
    /// @brief Seeded from Havok's reflection (size 0x38).
    class hkpBvShape : public hkpShape
    {
    public:
        hkpShape* m_boundingVolumeShape;   ///< +0x20
        hkpSingleShapeContainer m_childShape;   ///< +0x28
    };
} }
