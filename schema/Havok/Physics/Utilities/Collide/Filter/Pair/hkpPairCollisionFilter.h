#pragma once

/// @file
/// @brief Layouts seeded from Havok's own reflection in the exe (tools/cbmatch --seed-headers): Havok's member names,
///        the gaps as explicit padding; tools/cbmatch --check-layout proves every named member.

#include <Common/Base/hkBase.h>
#include <Physics/Collide/Filter/Group/hkpGroupFilter.h>

namespace CB { namespace schema
{
    /// @brief Seeded from Havok's reflection (size 0x60).
    class hkpPairCollisionFilter : public hkpCollisionFilter
    {
    public:
        /// @brief Seeded from Havok's reflection (size 0x10).
        struct MapPairFilterKeyOverrideType
        {
        public:
            void* m_elem;   ///< +0x0, not serialized
            hkInt32 m_numElems;   ///< +0x8
            hkInt32 m_hashMod;   ///< +0xc
        };

        MapPairFilterKeyOverrideType m_disabledPairs;   ///< +0x48, not serialized
        hkpCollisionFilter* m_childFilter;   ///< +0x58
    };
} }
