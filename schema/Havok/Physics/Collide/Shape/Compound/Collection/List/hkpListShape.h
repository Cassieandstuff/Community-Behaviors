#pragma once

/// @file
/// @brief Layouts seeded from Havok's own reflection in the exe (tools/cbmatch --seed-headers): Havok's member names,
///        the gaps as explicit padding; tools/cbmatch --check-layout proves every named member.

#include <Common/Base/hkBase.h>
#include <Physics/Collide/Shape/Compound/Collection/hkpShapeCollection.h>
#include <Physics/Collide/Shape/hkpShape.h>

namespace CB { namespace schema
{
    /// @brief Seeded from Havok's reflection (size 0x20).
    struct hkpListShapeChildInfo
    {
    public:
        hkpShape* m_shape;   ///< +0x0
        hkUint32 m_collisionFilterInfo;   ///< +0x8
        hkInt32 m_shapeSize;   ///< +0xc, not serialized
        hkInt32 m_numChildShapes;   ///< +0x10, not serialized
        hkUint8 m_pad0x14[0xc];
    };

    /// @brief Seeded from Havok's reflection (size 0x90).
    class hkpListShape : public hkpShapeCollection
    {
    public:
        enum ListShapeFlags
        {
            ALL_FLAGS_CLEAR = 0,
            DISABLE_SPU_CACHE_FOR_LIST_CHILD_INFO = 1,
        };
        hkArray<hkpListShapeChildInfo> m_childInfo;   ///< +0x30
        hkUint16 m_flags;   ///< +0x40
        hkUint16 m_numDisabledChildren;   ///< +0x42
        hkVector4 m_aabbHalfExtents;   ///< +0x50
        hkVector4 m_aabbCenter;   ///< +0x60
        hkUint32 m_enabledChildren[8];   ///< +0x70
    };
} }
