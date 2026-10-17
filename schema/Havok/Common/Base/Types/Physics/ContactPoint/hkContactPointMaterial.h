#pragma once

/// @file
/// @brief Layouts seeded from Havok's own reflection in the exe (tools/cbmatch --seed-headers): Havok's member names,
///        the gaps as explicit padding; tools/cbmatch --check-layout proves every named member.

#include <Common/Base/hkBase.h>

namespace CB { namespace schema
{
    /// @brief Seeded from Havok's reflection (size 0x10).
    struct hkContactPointMaterial
    {
    public:
        enum FlagEnum
        {
            CONTACT_IS_NEW = 1,
            CONTACT_USES_SOLVER_PATH2 = 2,
            CONTACT_BREAKOFF_OBJECT_ID = 4,
            CONTACT_IS_DISABLED = 8,
        };
        hkUlong m_userData;   ///< +0x0
        hkUint8 m_friction;   ///< +0x8
        hkUint8 m_restitution;   ///< +0x9
        hkUint8 m_maxImpulse;   ///< +0xa
        hkUint8 m_flags;   ///< +0xb
    };
} }
