#pragma once

/// @file
/// @brief Layouts seeded from Havok's own reflection in the exe (tools/cbmatch --seed-headers): Havok's member names,
///        the gaps as explicit padding; tools/cbmatch --check-layout proves every named member.

#include <Common/Base/Object/hkReferencedObject.h>
#include <Common/Base/hkBase.h>
#include <Common/SceneData/Mesh/hkxVertexDescription.h>

namespace CB { namespace schema
{
    /// @brief Seeded from Havok's reflection (size 0x88, version 1).
    class hkxVertexBuffer : public hkReferencedObject
    {
    public:
        /// @brief Seeded from Havok's reflection (size 0x68).
        struct VertexData
        {
        public:
            hkArray<hkVector4> m_vectorData;   ///< +0x0
            hkArray<hkReal> m_floatData;   ///< +0x10
            hkArray<hkUint32> m_uint32Data;   ///< +0x20
            hkArray<hkUint16> m_uint16Data;   ///< +0x30
            hkArray<hkUint8> m_uint8Data;   ///< +0x40
            hkUint32 m_numVerts;   ///< +0x50
            hkUint32 m_vectorStride;   ///< +0x54
            hkUint32 m_floatStride;   ///< +0x58
            hkUint32 m_uint32Stride;   ///< +0x5c
            hkUint32 m_uint16Stride;   ///< +0x60
            hkUint32 m_uint8Stride;   ///< +0x64
        };

        VertexData m_data;   ///< +0x10
        hkxVertexDescription m_desc;   ///< +0x78
    };
} }
