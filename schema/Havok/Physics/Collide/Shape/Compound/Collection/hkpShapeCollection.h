#pragma once

/// @file
/// @brief Layouts seeded from Havok's own reflection in the exe (tools/cbmatch --seed-headers): Havok's member names,
///        the gaps as explicit padding; tools/cbmatch --check-layout proves every named member.

#include <Common/Base/hkBase.h>
#include <Physics/Collide/Shape/hkpShape.h>

namespace CB { namespace schema
{
    /// @brief Seeded from Havok's reflection (size 0x30).
    class hkpShapeCollection : public hkpShape
    {
    public:
        enum CollectionType
        {
            COLLECTION_LIST = 0,
            COLLECTION_EXTENDED_MESH = 1,
            COLLECTION_TRISAMPLED_HEIGHTFIELD = 2,
            COLLECTION_USER = 3,
            COLLECTION_SIMPLE_MESH = 4,
            COLLECTION_MESH_SHAPE = 5,
            COLLECTION_COMPRESSED_MESH = 6,
            COLLECTION_MAX = 7,
        };
        hkUint8 m_pad0x20[0x8];
        hkBool m_disableWelding;   ///< +0x28
        hkUint8 m_collectionType;   ///< +0x29
    };
} }
