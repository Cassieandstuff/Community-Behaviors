#pragma once

/// @file
/// @brief Layouts seeded from Havok's own reflection in the exe (tools/cbmatch --seed-headers): Havok's member names,
///        the gaps as explicit padding; tools/cbmatch --check-layout proves every named member.

#include <Common/Base/Object/hkReferencedObject.h>
#include <Common/Base/hkBase.h>

namespace CB { namespace schema
{
    class hkpPhysicsSystem;
    class hkpWorldCinfo;

    /// @brief Seeded from Havok's reflection (size 0x28).
    class hkpPhysicsData : public hkReferencedObject
    {
    public:
        hkpWorldCinfo* m_worldCinfo;   ///< +0x10
        hkArray<hkpPhysicsSystem*> m_systems;   ///< +0x18
    };
} }
