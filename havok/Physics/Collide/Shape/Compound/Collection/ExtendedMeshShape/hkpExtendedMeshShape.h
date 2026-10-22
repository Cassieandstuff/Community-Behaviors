#pragma once

/// @file
/// @brief Layouts seeded from Havok's own reflection in the exe (tools/cbmatch --seed-headers): Havok's member names,
///        the gaps as explicit padding; tools/cbmatch --check-layout proves every named member.

#include <Common/Base/hkBase.h>
#include <Physics/Collide/Shape/Compound/Collection/hkpShapeCollection.h>
#include <Physics/Collide/Shape/Convex/hkpConvexShape.h>

namespace CB { namespace havok
{
    /// @brief Seeded from Havok's reflection (size 0x150, version 3).
    class hkpExtendedMeshShape : public hkpShapeCollection
    {
    public:
        /// @brief Seeded from Havok's reflection (size 0x28, version 2).
        struct Subpart
        {
        public:
            hkInt8 m_type;   ///< +0x0
            hkInt8 m_materialIndexStridingType;   ///< +0x1
            hkInt16 m_materialStriding;   ///< +0x2, not serialized
            void* m_materialIndexBase;   ///< +0x8, not serialized
            hkUint16 m_materialIndexStriding;   ///< +0x10
            hkUint16 m_numMaterials;   ///< +0x12
            void* m_materialBase;   ///< +0x18, not serialized
            hkUlong m_userData;   ///< +0x20
        };

        /// @brief Seeded from Havok's reflection (size 0x60, version 1).
        struct ShapesSubpart : public Subpart
        {
        public:
            hkArray<hkpConvexShape*> m_childShapes;   ///< +0x28
            hkQuaternion m_rotation;   ///< +0x40
            hkVector4 m_translation;   ///< +0x50
        };

        /// @brief Seeded from Havok's reflection (size 0xa0, version 3).
        struct TrianglesSubpart : public Subpart
        {
        public:
            hkInt32 m_numTriangleShapes;   ///< +0x28
            void* m_vertexBase;   ///< +0x30, not serialized
            hkInt32 m_numVertices;   ///< +0x38
            void* m_indexBase;   ///< +0x40, not serialized
            hkUint16 m_vertexStriding;   ///< +0x48
            hkInt32 m_triangleOffset;   ///< +0x4c
            hkUint16 m_indexStriding;   ///< +0x50
            hkInt8 m_stridingType;   ///< +0x52
            hkInt8 m_flipAlternateTriangles;   ///< +0x53
            hkVector4 m_extrusion;   ///< +0x60
            hkQsTransform m_transform;   ///< +0x70
        };

        enum IndexStridingType
        {
            INDICES_INVALID = 0,
            INDICES_INT8 = 1,
            INDICES_INT16 = 2,
            INDICES_INT32 = 3,
            INDICES_MAX_ID = 4,
        };
        enum MaterialIndexStridingType
        {
            MATERIAL_INDICES_INVALID = 0,
            MATERIAL_INDICES_INT8 = 1,
            MATERIAL_INDICES_INT16 = 2,
            MATERIAL_INDICES_MAX_ID = 3,
        };
        enum SubpartType
        {
            SUBPART_TRIANGLES = 0,
            SUBPART_SHAPE = 1,
        };
        TrianglesSubpart m_embeddedTrianglesSubpart;   ///< +0x30
        hkVector4 m_aabbHalfExtents;   ///< +0xd0
        hkVector4 m_aabbCenter;   ///< +0xe0
        void* m_materialClass;   ///< +0xf0, not serialized
        hkInt32 m_numBitsForSubpartIndex;   ///< +0xf8
        hkArray<TrianglesSubpart> m_trianglesSubparts;   ///< +0x100
        hkArray<ShapesSubpart> m_shapesSubparts;   ///< +0x110
        hkArray<hkUint16> m_weldingInfo;   ///< +0x120
        hkUint8 m_weldingType;   ///< +0x130
        hkUint32 m_defaultCollisionFilterInfo;   ///< +0x134
        hkInt32 m_cachedNumChildShapes;   ///< +0x138
        hkReal m_triangleRadius;   ///< +0x13c
        hkInt32 m_padding;   ///< +0x140, not serialized
    };
} }
