#pragma once

/// @file
/// @brief Layouts seeded from Havok's own reflection in the exe (tools/cbmatch --seed-headers): Havok's member names,
///        the gaps as explicit padding; tools/cbmatch --check-layout proves every named member.

#include <Common/Base/hkBase.h>
#include <Physics/Collide/Filter/Group/hkpGroupFilter.h>

namespace CB { namespace havok
{
    /// @brief Seeded from Havok's reflection (size 0xd0).
    class hkpGroupCollisionFilter : public hkpCollisionFilter
    {
    public:
        hkBool m_noGroupCollisionEnabled;   ///< +0x48
        hkUint32 m_collisionGroups[32];   ///< +0x4c
    };
} }
