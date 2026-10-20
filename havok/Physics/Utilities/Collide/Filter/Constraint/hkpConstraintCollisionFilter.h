#pragma once

/// @file
/// @brief Layouts seeded from Havok's own reflection in the exe (tools/cbmatch --seed-headers): Havok's member names,
///        the gaps as explicit padding; tools/cbmatch --check-layout proves every named member.

#include <Common/Base/hkBase.h>
#include <Physics/Utilities/Collide/Filter/Pair/hkpPairCollisionFilter.h>

namespace CB { namespace havok
{
    /// @brief Seeded from Havok's reflection (size 0x68).
    class hkpConstraintCollisionFilter : public hkpPairCollisionFilter
    {
    public:
        hkUint8 m_pad0x60[0x8];
    };
} }
