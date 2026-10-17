#pragma once

/// @file
/// @brief Layouts seeded from Havok's own reflection in the exe (tools/cbmatch --seed-headers): Havok's member names,
///        the gaps as explicit padding; tools/cbmatch --check-layout proves every named member.

#include <Common/Base/hkBase.h>

namespace CB { namespace schema
{
    struct hkxAttribute;

    /// @brief Seeded from Havok's reflection (size 0x18).
    struct hkxAttributeGroup
    {
    public:
        hkStringPtr m_name;   ///< +0x0
        hkArray<hkxAttribute> m_attributes;   ///< +0x8
    };
} }
