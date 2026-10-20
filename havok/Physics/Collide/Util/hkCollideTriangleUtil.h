/// @file
/// @brief hkCollideTriangleUtil: the per-triangle constants the triangle collision tests share.
#pragma once

#include <Common/Base/hkBase.h>

namespace CB { namespace havok
{
    namespace hkCollideTriangleUtil
    {
        /// @note 0x14 bytes, as the Havok SDK's.
        struct PointTriangleDistanceCache
        {
            hkReal m_invEdgeLen[3];     ///< +0x00
            hkReal m_invTriNormalLen;   ///< +0x0c
            hkReal m_normalLen;         ///< +0x10
        };

        /// @brief Fills `cache` for the triangle `triVertices` (3 points).
        /// @note Name per the Havok SDK; out of line in vanilla (0x140c79de0).
        void setupPointTriangleDistanceCache(const hkVector4* triVertices, PointTriangleDistanceCache& cache);
    }
} }
