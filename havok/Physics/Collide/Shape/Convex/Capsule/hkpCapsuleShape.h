/// @file
/// @brief hkpCapsuleShape: a segment with a radius.
#pragma once

#include <Physics/Collide/Shape/Convex/hkpConvexShape.h>

namespace CB { namespace havok
{
    /// @note 0x50 bytes (schema/physics/hkpCapsuleShape.yaml); vtable 0x141967988. Each vertex's w holds the radius.
    class hkpCapsuleShape : public hkpConvexShape
    {
    public:
        hkVector4 m_vertexA;   ///< +0x30
        hkVector4 m_vertexB;   ///< +0x40

        /// @note Out of line in vanilla (0x140b1eed0); `radius` arrives in xmm3.
        hkpCapsuleShape(const hkVector4& vertexA, const hkVector4& vertexB, hkReal radius);

        HK_FORCE_INLINE const hkVector4& getVertex(int i) const { return (&m_vertexA)[i]; }
    };
} }
