#pragma once

/// @file
/// @brief Layouts seeded from Havok's own reflection in the exe (tools/cbmatch --seed-headers): Havok's member names,
///        the gaps as explicit padding; tools/cbmatch --check-layout proves every named member.

#include <Common/Base/hkBase.h>
#include <Physics/Collide/Shape/Convex/hkpConvexShape.h>

namespace CB { namespace havok
{
    class hkpConvexVerticesConnectivity;
    struct hkpConvexVerticesShapeFourVectors;

    /// @brief Seeded from Havok's reflection (size 0x90, version 3).
    class hkpConvexVerticesShape : public hkpConvexShape
    {
    public:
        hkVector4 m_aabbHalfExtents;   ///< +0x30
        hkVector4 m_aabbCenter;   ///< +0x40
        hkArray<hkpConvexVerticesShapeFourVectors> m_rotatedVertices;   ///< +0x50
        hkInt32 m_numVertices;   ///< +0x60
        void* m_externalObject;   ///< +0x68, not serialized
        void* m_getFaceNormals;   ///< +0x70, not serialized
        hkArray<hkVector4> m_planeEquations;   ///< +0x78
        hkpConvexVerticesConnectivity* m_connectivity;   ///< +0x88
    };
} }
