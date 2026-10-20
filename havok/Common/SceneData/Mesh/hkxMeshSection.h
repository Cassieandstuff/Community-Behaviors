#pragma once

/// @file
/// @brief Layouts seeded from Havok's own reflection in the exe (tools/cbmatch --seed-headers): Havok's member names,
///        the gaps as explicit padding; tools/cbmatch --check-layout proves every named member.

#include <Common/Base/Object/hkReferencedObject.h>
#include <Common/Base/hkBase.h>

namespace CB { namespace havok
{
    class hkxIndexBuffer;
    class hkxMaterial;
    class hkxVertexBuffer;

    /// @brief Seeded from Havok's reflection (size 0x40, version 1).
    class hkxMeshSection : public hkReferencedObject
    {
    public:
        hkxVertexBuffer* m_vertexBuffer;   ///< +0x10
        hkArray<hkxIndexBuffer*> m_indexBuffers;   ///< +0x18
        hkxMaterial* m_material;   ///< +0x28
        hkArray<hkReferencedObject*> m_userChannels;   ///< +0x30
    };
} }
