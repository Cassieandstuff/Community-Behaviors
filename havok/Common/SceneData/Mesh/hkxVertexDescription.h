#pragma once

/// @file
/// @brief Layouts seeded from Havok's own reflection in the exe (tools/cbmatch --seed-headers): Havok's member names,
///        the gaps as explicit padding; tools/cbmatch --check-layout proves every named member.

#include <Common/Base/hkBase.h>

namespace CB { namespace havok
{
    /// @brief Seeded from Havok's reflection (size 0x10, version 1).
    struct hkxVertexDescription
    {
    public:
        /// @brief Seeded from Havok's reflection (size 0x10, version 2).
        struct ElementDecl
        {
        public:
            hkUint32 m_byteOffset;   ///< +0x0
            hkUint16 m_type;   ///< +0x4
            hkUint16 m_usage;   ///< +0x6
            hkUint32 m_byteStride;   ///< +0x8
            hkUint8 m_numElements;   ///< +0xc
        };

        enum DataType
        {
            HKX_DT_NONE = 0,
            HKX_DT_UINT8 = 1,
            HKX_DT_INT16 = 2,
            HKX_DT_UINT32 = 3,
            HKX_DT_FLOAT = 4,
        };
        enum DataUsage
        {
            HKX_DU_NONE = 0,
            HKX_DU_POSITION = 1,
            HKX_DU_COLOR = 2,
            HKX_DU_NORMAL = 4,
            HKX_DU_TANGENT = 8,
            HKX_DU_BINORMAL = 16,
            HKX_DU_TEXCOORD = 32,
            HKX_DU_BLENDWEIGHTS = 64,
            HKX_DU_BLENDINDICES = 128,
            HKX_DU_USERDATA = 256,
        };
        hkArray<ElementDecl> m_decls;   ///< +0x0
    };
} }
