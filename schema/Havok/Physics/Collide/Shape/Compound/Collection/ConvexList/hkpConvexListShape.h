#pragma once

/// @file
/// @brief Layouts seeded from Havok's own reflection in the exe (tools/cbmatch --seed-headers): Havok's member names,
///        the gaps as explicit padding; tools/cbmatch --check-layout proves every named member.

#include <Common/Base/hkBase.h>
#include <Physics/Collide/Shape/Convex/hkpConvexShape.h>

namespace CB { namespace schema
{
    /// @brief Seeded from Havok's reflection (size 0x80).
    class hkpConvexListShape : public hkpConvexShape
    {
    public:
        hkUint8 m_pad0x28[0x8];
        hkReal m_minDistanceToUseConvexHullForGetClosestPoints;   ///< +0x30
        hkVector4 m_aabbHalfExtents;   ///< +0x40
        hkVector4 m_aabbCenter;   ///< +0x50
        hkBool m_useCachedAabb;   ///< +0x60
        hkArray<hkpConvexShape*> m_childShapes;   ///< +0x68
    };
} }
