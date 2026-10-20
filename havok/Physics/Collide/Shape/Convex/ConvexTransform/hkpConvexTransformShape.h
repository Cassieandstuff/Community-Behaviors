#pragma once

/// @file
/// @brief Layouts seeded from Havok's own reflection in the exe (tools/cbmatch --seed-headers): Havok's member names,
///        the gaps as explicit padding; tools/cbmatch --check-layout proves every named member.

#include <Common/Base/hkBase.h>
#include <Physics/Collide/Shape/Convex/hkpConvexShape.h>
#include <Physics/Collide/Shape/hkpShapeContainer.h>

namespace CB { namespace havok
{
    /// @brief Seeded from Havok's reflection (size 0x40).
    class hkpConvexTransformShapeBase : public hkpConvexShape
    {
    public:
        hkpSingleShapeContainer m_childShape;   ///< +0x28
        hkInt32 m_childShapeSize;   ///< +0x38, not serialized
    };

    /// @brief Seeded from Havok's reflection (size 0x80).
    class hkpConvexTransformShape : public hkpConvexTransformShapeBase
    {
    public:
        hkTransform m_transform;   ///< +0x40
    };
} }
