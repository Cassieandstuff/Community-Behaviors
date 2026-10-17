#pragma once

/// @file
/// @brief Layouts seeded from Havok's own reflection in the exe (tools/cbmatch --seed-headers): Havok's member names,
///        the gaps as explicit padding; tools/cbmatch --check-layout proves every named member.

#include <Common/Base/hkBase.h>
#include <Physics/Collide/Shape/Convex/hkpConvexShape.h>

namespace CB { namespace schema
{
    /// @brief Seeded from Havok's reflection (size 0x70).
    class hkpCylinderShape : public hkpConvexShape
    {
    public:
        enum VertexIdEncoding
        {
            VERTEX_ID_ENCODING_IS_BASE_A_SHIFT = 7,
            VERTEX_ID_ENCODING_SIN_SIGN_SHIFT = 6,
            VERTEX_ID_ENCODING_COS_SIGN_SHIFT = 5,
            VERTEX_ID_ENCODING_IS_SIN_LESSER_SHIFT = 4,
            VERTEX_ID_ENCODING_VALUE_MASK = 15,
        };
        hkReal m_cylRadius;   ///< +0x28
        hkReal m_cylBaseRadiusFactorForHeightFieldCollisions;   ///< +0x2c
        hkVector4 m_vertexA;   ///< +0x30
        hkVector4 m_vertexB;   ///< +0x40
        hkVector4 m_perpendicular1;   ///< +0x50
        hkVector4 m_perpendicular2;   ///< +0x60
    };
} }
