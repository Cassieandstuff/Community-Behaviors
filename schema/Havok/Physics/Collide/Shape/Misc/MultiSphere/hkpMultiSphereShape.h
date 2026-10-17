#pragma once

/// @file
/// @brief Layouts seeded from Havok's own reflection in the exe (tools/cbmatch --seed-headers): Havok's member names,
///        the gaps as explicit padding; tools/cbmatch --check-layout proves every named member.

#include <Common/Base/hkBase.h>
#include <Physics/Collide/Shape/Convex/hkpConvexShape.h>

namespace CB { namespace schema
{
    /// @brief Seeded from Havok's reflection (size 0xb0).
    class hkpMultiSphereShape : public hkpSphereRepShape
    {
    public:
        hkInt32 m_numSpheres;   ///< +0x20
        hkVector4 m_spheres[8];   ///< +0x30
    };
} }
