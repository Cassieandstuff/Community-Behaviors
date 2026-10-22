#pragma once

/// @file
/// @brief Layouts seeded from Havok's own reflection in the exe (tools/cbmatch --seed-headers): Havok's member names,
///        the gaps as explicit padding; tools/cbmatch --check-layout proves every named member.

#include <Common/Base/hkBase.h>
#include <Physics/Collide/Filter/Group/hkpGroupFilter.h>
#include <Physics/Dynamics/Entity/hkpEntity.h>

namespace CB { namespace havok
{
    /// @brief Seeded from Havok's reflection (size 0x60).
    class hkpDisableEntityCollisionFilter : public hkpCollisionFilter
    {
    public:
        hkUint8 m_pad0x48[0x8];
        hkArray<hkpEntity*> m_disabledEntities;   ///< +0x50
    };
} }
