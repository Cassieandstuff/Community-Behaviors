#pragma once

/// @file
/// @brief Layouts seeded from Havok's own reflection in the exe (tools/cbmatch --seed-headers): Havok's member names,
///        the gaps as explicit padding; tools/cbmatch --check-layout proves every named member.

#include <Common/Base/Object/hkReferencedObject.h>
#include <Common/Base/hkBase.h>

namespace CB { namespace schema
{
    /// @brief Seeded from Havok's reflection (size 0x30).
    class hkpAction : public hkReferencedObject
    {
    public:
        void* m_world;   ///< +0x10, not serialized
        void* m_island;   ///< +0x18, not serialized
        hkUlong m_userData;   ///< +0x20
        hkStringPtr m_name;   ///< +0x28
    };
} }
