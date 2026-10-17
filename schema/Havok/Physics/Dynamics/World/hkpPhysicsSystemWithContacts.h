#pragma once

/// @file
/// @brief Layouts seeded from Havok's own reflection in the exe (tools/cbmatch --seed-headers): Havok's member names,
///        the gaps as explicit padding; tools/cbmatch --check-layout proves every named member.

#include <Common/Base/hkBase.h>
#include <Physics/Dynamics/World/hkpPhysicsSystem.h>

namespace CB { namespace schema
{
    class hkpSerializedAgentNnEntry;

    /// @brief Seeded from Havok's reflection (size 0x78).
    class hkpPhysicsSystemWithContacts : public hkpPhysicsSystem
    {
    public:
        hkArray<hkpSerializedAgentNnEntry*> m_contacts;   ///< +0x68
    };
} }
