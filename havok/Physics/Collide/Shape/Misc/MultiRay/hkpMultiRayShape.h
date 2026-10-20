#pragma once

/// @file
/// @brief Layouts seeded from Havok's own reflection in the exe (tools/cbmatch --seed-headers): Havok's member names,
///        the gaps as explicit padding; tools/cbmatch --check-layout proves every named member.

#include <Common/Base/hkBase.h>
#include <Physics/Collide/Shape/hkpShape.h>

namespace CB { namespace havok
{
    /// @brief Seeded from Havok's reflection (size 0x38).
    class hkpMultiRayShape : public hkpShape
    {
    public:
        /// @brief Seeded from Havok's reflection (size 0x20).
        struct Ray
        {
        public:
            hkVector4 m_start;   ///< +0x0
            hkVector4 m_end;   ///< +0x10
        };

        hkArray<Ray> m_rays;   ///< +0x20
        hkReal m_rayPenetrationDistance;   ///< +0x30
    };
} }
