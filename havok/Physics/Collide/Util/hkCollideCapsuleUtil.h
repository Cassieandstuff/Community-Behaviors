/// @file
/// @brief hkCollideCapsuleUtil: the capsule collision tests.
#pragma once

#include <Physics/Collide/Util/hkCollideTriangleUtil.h>

namespace CB { namespace havok
{
    struct hkContactPoint;
    struct hkpFeatureOutput;

    /// @brief Collides the capsule `capsAPoints` (2 points) of radius `capsARadius` with the triangle `triVertices`
    ///        (3 points) of radius `triBRadius`: up to 3 contact points in `points` (a separating normal's w is the
    ///        distance), `cache` from hkCollideTriangleUtil::setupPointTriangleDistanceCache.
    /// @note Name per the Havok SDK; out of line in vanilla (0x140c7aa10).
    void hkCollideCapsuleUtilCapsVsTri(const hkVector4* capsAPoints, hkReal capsARadius, const hkVector4* triVertices, hkReal triBRadius,
                                       const hkCollideTriangleUtil::PointTriangleDistanceCache& cache, hkReal collisionTolerance,
                                       int searchManifold, hkContactPoint* points, hkpFeatureOutput* featuresOut);
} }
