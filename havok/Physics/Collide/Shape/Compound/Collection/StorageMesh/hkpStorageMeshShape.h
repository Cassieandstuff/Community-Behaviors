#pragma once

/// @file
/// @brief Layouts seeded from Havok's own reflection in the exe (tools/cbmatch --seed-headers): Havok's member names,
///        the gaps as explicit padding; tools/cbmatch --check-layout proves every named member.

#include <Common/Base/Object/hkReferencedObject.h>
#include <Common/Base/hkBase.h>
#include <Physics/Collide/Shape/Compound/Collection/Mesh/hkpMeshShape.h>

namespace CB { namespace havok
{
    /// @brief Seeded from Havok's reflection (size 0x90).
    class hkpStorageMeshShape : public hkpMeshShape
    {
    public:
        /// @brief Seeded from Havok's reflection (size 0x70).
        class SubpartStorage : public hkReferencedObject
        {
        public:
            hkArray<hkReal> m_vertices;   ///< +0x10
            hkArray<hkUint16> m_indices16;   ///< +0x20
            hkArray<hkUint32> m_indices32;   ///< +0x30
            hkArray<hkUint8> m_materialIndices;   ///< +0x40
            hkArray<hkUint32> m_materials;   ///< +0x50
            hkArray<hkUint16> m_materialIndices16;   ///< +0x60
        };

        hkArray<SubpartStorage*> m_storage;   ///< +0x80
    };
} }
