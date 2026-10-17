#pragma once

/// @file
/// @brief Layouts seeded from Havok's own reflection in the exe (tools/cbmatch --seed-headers): Havok's member names,
///        the gaps as explicit padding; tools/cbmatch --check-layout proves every named member.

#include <Common/Base/hkBase.h>
#include <Physics/Collide/Shape/Compound/Collection/hkpShapeCollection.h>

namespace CB { namespace schema
{
    /// @brief Seeded from Havok's reflection (size 0x80).
    class hkpMeshShape : public hkpShapeCollection
    {
    public:
        /// @brief Seeded from Havok's reflection (size 0x50).
        struct Subpart
        {
        public:
            void* m_vertexBase;   ///< +0x0, not serialized
            hkInt32 m_vertexStriding;   ///< +0x8
            hkInt32 m_numVertices;   ///< +0xc
            void* m_indexBase;   ///< +0x10, not serialized
            hkInt8 m_stridingType;   ///< +0x18
            hkInt8 m_materialIndexStridingType;   ///< +0x19
            hkInt32 m_indexStriding;   ///< +0x1c
            hkInt32 m_flipAlternateTriangles;   ///< +0x20
            hkInt32 m_numTriangles;   ///< +0x24
            void* m_materialIndexBase;   ///< +0x28, not serialized
            hkInt32 m_materialIndexStriding;   ///< +0x30
            void* m_materialBase;   ///< +0x38, not serialized
            hkInt32 m_materialStriding;   ///< +0x40
            hkInt32 m_numMaterials;   ///< +0x44
            hkInt32 m_triangleOffset;   ///< +0x48
        };

        enum MeshShapeIndexStridingType
        {
            INDICES_INVALID = 0,
            INDICES_INT16 = 1,
            INDICES_INT32 = 2,
            INDICES_MAX_ID = 3,
        };
        enum MeshShapeMaterialIndexStridingType
        {
            MATERIAL_INDICES_INVALID = 0,
            MATERIAL_INDICES_INT8 = 1,
            MATERIAL_INDICES_INT16 = 2,
            MATERIAL_INDICES_MAX_ID = 3,
        };
        hkVector4 m_scaling;   ///< +0x30
        hkInt32 m_numBitsForSubpartIndex;   ///< +0x40
        hkArray<Subpart> m_subparts;   ///< +0x48
        hkArray<hkUint16> m_weldingInfo;   ///< +0x58
        hkUint8 m_weldingType;   ///< +0x68
        hkReal m_radius;   ///< +0x6c
        hkInt32 m_pad[3];   ///< +0x70
    };
} }
