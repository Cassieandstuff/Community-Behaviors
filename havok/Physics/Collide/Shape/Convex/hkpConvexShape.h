/// @file
/// @brief hkpSphereRepShape / hkpConvexShape: the convex shapes' base (a shell radius around the core shape).
#pragma once

#include <Physics/Collide/Shape/hkpShape.h>

namespace CB { namespace havok
{
    /// @note 0x20 bytes, no members of its own (schema/physics/hkpSphereRepShape.yaml).
    class hkpSphereRepShape : public hkpShape
    {
    };

    /// @note 0x28 bytes (schema/physics/hkpConvexShape.yaml).
    class hkpConvexShape : public hkpSphereRepShape
    {
    public:
        hkReal m_radius;   ///< +0x20 — the convex radius (shell) around the core shape

        HK_FORCE_INLINE hkReal getRadius() const { return m_radius; }
    };
} }
