#pragma once

/// @file
/// @brief Layouts seeded from Havok's own reflection in the exe (tools/cbmatch --seed-headers): Havok's member names,
///        the gaps as explicit padding; tools/cbmatch --check-layout proves every named member.

#include <Common/Base/Object/hkReferencedObject.h>
#include <Common/Base/hkBase.h>
#include <Physics/Collide/Shape/hkpShape.h>

namespace CB { namespace schema
{
    /// @brief Seeded from Havok's reflection (size 0x80).
    class hkpShapeInfo : public hkReferencedObject
    {
    public:
        hkpShape* m_shape;   ///< +0x10
        hkBool m_isHierarchicalCompound;   ///< +0x18
        hkBool m_hkdShapesCollected;   ///< +0x19
        hkArray<hkStringPtr> m_childShapeNames;   ///< +0x20
        hkArray<hkTransform> m_childTransforms;   ///< +0x30
        hkTransform m_transform;   ///< +0x40
    };
} }
