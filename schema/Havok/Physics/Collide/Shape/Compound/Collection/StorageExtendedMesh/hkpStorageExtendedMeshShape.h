#pragma once

/// @file
/// @brief Layouts seeded from Havok's own reflection in the exe (tools/cbmatch --seed-headers): Havok's member names,
///        the gaps as explicit padding; tools/cbmatch --check-layout proves every named member.

#include <Common/Base/Object/hkReferencedObject.h>
#include <Common/Base/hkBase.h>
#include <Physics/Collide/Shape/Compound/Collection/ExtendedMeshShape/hkpExtendedMeshShape.h>
#include <Physics/Collide/Shape/Compound/Collection/Mesh/hkpMeshMaterial.h>

namespace CB { namespace schema
{
    struct hkpNamedMeshMaterial;

    /// @brief Seeded from Havok's reflection (size 0x170).
    class hkpStorageExtendedMeshShape : public hkpExtendedMeshShape
    {
    public:
        /// @brief Seeded from Havok's reflection (size 0x10, version 1).
        struct Material : public hkpMeshMaterial
        {
        public:
            hkHalf m_restitution;   ///< +0x4
            hkHalf m_friction;   ///< +0x6
            hkUlong m_userData;   ///< +0x8
        };

        /// @brief Seeded from Havok's reflection (size 0x90, version 3).
        class MeshSubpartStorage : public hkReferencedObject
        {
        public:
            hkArray<hkVector4> m_vertices;   ///< +0x10
            hkArray<hkUint8> m_indices8;   ///< +0x20
            hkArray<hkUint16> m_indices16;   ///< +0x30
            hkArray<hkUint32> m_indices32;   ///< +0x40
            hkArray<hkUint8> m_materialIndices;   ///< +0x50
            hkArray<Material> m_materials;   ///< +0x60
            hkArray<hkpNamedMeshMaterial> m_namedMaterials;   ///< +0x70
            hkArray<hkUint16> m_materialIndices16;   ///< +0x80
        };

        /// @brief Seeded from Havok's reflection (size 0x40, version 2).
        class ShapeSubpartStorage : public hkReferencedObject
        {
        public:
            hkArray<hkUint8> m_materialIndices;   ///< +0x10
            hkArray<Material> m_materials;   ///< +0x20
            hkArray<hkUint16> m_materialIndices16;   ///< +0x30
        };

        hkArray<MeshSubpartStorage*> m_meshstorage;   ///< +0x150
        hkArray<ShapeSubpartStorage*> m_shapestorage;   ///< +0x160
    };
} }
