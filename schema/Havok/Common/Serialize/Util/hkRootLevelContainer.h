#pragma once

/// @file
/// @brief Layouts seeded from Havok's own reflection in the exe (tools/cbmatch --seed-headers): Havok's member names,
///        the gaps as explicit padding; tools/cbmatch --check-layout proves every named member.

#include <Common/Base/hkBase.h>

namespace CB { namespace schema
{
    struct hkRootLevelContainerNamedVariant;

    /// @brief Seeded from Havok's reflection (size 0x10).
    struct hkRootLevelContainer
    {
    public:
        hkArray<hkRootLevelContainerNamedVariant> m_namedVariants;   ///< +0x0
    };
} }
