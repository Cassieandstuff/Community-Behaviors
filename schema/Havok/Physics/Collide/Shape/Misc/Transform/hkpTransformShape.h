#pragma once

/// @file
/// @brief Layouts seeded from Havok's own reflection in the exe (tools/cbmatch --seed-headers): Havok's member names,
///        the gaps as explicit padding; tools/cbmatch --check-layout proves every named member.

#include <Common/Base/hkBase.h>
#include <Physics/Collide/Shape/hkpShape.h>
#include <Physics/Collide/Shape/hkpShapeContainer.h>

namespace CB { namespace schema
{
    /// @brief Seeded from Havok's reflection (size 0x90).
    class hkpTransformShape : public hkpShape
    {
    public:
        hkpSingleShapeContainer m_childShape;   ///< +0x20
        hkInt32 m_childShapeSize;   ///< +0x30, not serialized
        hkQuaternion m_rotation;   ///< +0x40
        hkTransform m_transform;   ///< +0x50
    };
} }
