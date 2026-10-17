#pragma once

/// @file
/// @brief Layouts seeded from Havok's own reflection in the exe (tools/cbmatch --seed-headers): Havok's member names,
///        the gaps as explicit padding; tools/cbmatch --check-layout proves every named member.

#include <Common/Base/Object/hkReferencedObject.h>
#include <Common/Base/hkBase.h>

namespace CB { namespace schema
{
    /// @brief Seeded from Havok's reflection (size 0x40, version 1).
    class hkxIndexBuffer : public hkReferencedObject
    {
    public:
        enum IndexType
        {
            INDEX_TYPE_INVALID = 0,
            INDEX_TYPE_TRI_LIST = 1,
            INDEX_TYPE_TRI_STRIP = 2,
            INDEX_TYPE_TRI_FAN = 3,
            INDEX_TYPE_MAX_ID = 4,
        };
        hkInt8 m_indexType;   ///< +0x10
        hkArray<hkUint16> m_indices16;   ///< +0x18
        hkArray<hkUint32> m_indices32;   ///< +0x28
        hkUint32 m_vertexBaseOffset;   ///< +0x38
        hkUint32 m_length;   ///< +0x3c
    };
} }
