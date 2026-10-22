#pragma once

/// @file
/// @brief Layouts seeded from Havok's own reflection in the exe (tools/cbmatch --seed-headers): Havok's member names,
///        the gaps as explicit padding; tools/cbmatch --check-layout proves every named member.

#include <Common/Base/Types/Geometry/Aabb/hkAabb.h>
#include <Common/Base/hkBase.h>
#include <Physics/Collide/Shape/Compound/Collection/hkpShapeCollection.h>

namespace CB { namespace havok
{
    struct hkpNamedMeshMaterial;

    /// @brief Seeded from Havok's reflection (size 0x130, version 9).
    class hkpCompressedMeshShape : public hkpShapeCollection
    {
    public:
        /// @brief Seeded from Havok's reflection (size 0x10, version 2).
        struct BigTriangle
        {
        public:
            hkUint16 m_a;   ///< +0x0
            hkUint16 m_b;   ///< +0x2
            hkUint16 m_c;   ///< +0x4
            hkUint32 m_material;   ///< +0x8
            hkUint16 m_weldingInfo;   ///< +0xc
            hkUint16 m_transformIndex;   ///< +0xe
        };

        /// @brief Seeded from Havok's reflection (size 0x60, version 4).
        struct Chunk
        {
        public:
            hkVector4 m_offset;   ///< +0x0
            hkArray<hkUint16> m_vertices;   ///< +0x10
            hkArray<hkUint16> m_indices;   ///< +0x20
            hkArray<hkUint16> m_stripLengths;   ///< +0x30
            hkArray<hkUint16> m_weldingInfo;   ///< +0x40
            hkUint32 m_materialInfo;   ///< +0x50
            hkUint16 m_reference;   ///< +0x54
            hkUint16 m_transformIndex;   ///< +0x56
        };

        /// @brief Seeded from Havok's reflection (size 0x50, version 3).
        struct ConvexPiece
        {
        public:
            hkVector4 m_offset;   ///< +0x0
            hkArray<hkUint16> m_vertices;   ///< +0x10
            hkArray<hkUint8> m_faceVertices;   ///< +0x20
            hkArray<hkUint16> m_faceOffsets;   ///< +0x30
            hkUint16 m_reference;   ///< +0x40
            hkUint16 m_transformIndex;   ///< +0x42
        };

        enum MaterialType
        {
            MATERIAL_NONE = 0,
            MATERIAL_SINGLE_VALUE_PER_CHUNK = 1,
            MATERIAL_ONE_BYTE_PER_TRIANGLE = 2,
            MATERIAL_TWO_BYTES_PER_TRIANGLE = 3,
            MATERIAL_FOUR_BYTES_PER_TRIANGLE = 4,
        };
        hkInt32 m_bitsPerIndex;   ///< +0x30
        hkInt32 m_bitsPerWIndex;   ///< +0x34
        hkInt32 m_wIndexMask;   ///< +0x38
        hkInt32 m_indexMask;   ///< +0x3c
        hkReal m_radius;   ///< +0x40
        hkUint8 m_weldingType;   ///< +0x44
        hkUint8 m_materialType;   ///< +0x45
        hkArray<hkUint32> m_materials;   ///< +0x48
        hkArray<hkUint16> m_materials16;   ///< +0x58
        hkArray<hkUint8> m_materials8;   ///< +0x68
        hkArray<hkQsTransform> m_transforms;   ///< +0x78
        hkArray<hkVector4> m_bigVertices;   ///< +0x88
        hkArray<BigTriangle> m_bigTriangles;   ///< +0x98
        hkArray<Chunk> m_chunks;   ///< +0xa8
        hkArray<ConvexPiece> m_convexPieces;   ///< +0xb8
        hkReal m_error;   ///< +0xc8
        hkAabb m_bounds;   ///< +0xd0
        hkUint32 m_defaultCollisionFilterInfo;   ///< +0xf0
        void* m_meshMaterials;   ///< +0xf8, not serialized
        hkUint16 m_materialStriding;   ///< +0x100
        hkUint16 m_numMaterials;   ///< +0x102
        hkArray<hkpNamedMeshMaterial> m_namedMaterials;   ///< +0x108
        hkVector4 m_scaling;   ///< +0x120
    };
} }
