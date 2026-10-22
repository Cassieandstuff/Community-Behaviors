/// @file
/// @brief hkpBoxShape: a box around the origin.
#pragma once

#include <Physics/Collide/Shape/Convex/hkpConvexShape.h>

namespace CB { namespace havok
{
    /// @note 0x40 bytes (schema/physics/hkpBoxShape.yaml).
    class hkpBoxShape : public hkpConvexShape
    {
    public:
        hkVector4 m_halfExtents;   ///< +0x30

        HK_FORCE_INLINE const hkVector4& getHalfExtents() const { return m_halfExtents; }
    };
} }
