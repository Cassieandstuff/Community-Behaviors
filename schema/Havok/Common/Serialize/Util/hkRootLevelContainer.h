#pragma once

/// @file
/// @brief Layouts seeded from Havok's own reflection in the exe (tools/cbmatch --seed-headers): Havok's member names,
///        the gaps as explicit padding; tools/cbmatch --check-layout proves every named member.

#include <Common/Base/hkBase.h>

namespace CB { namespace schema
{
    /// @brief Seeded from Havok's reflection (size 0x10).
    struct hkRootLevelContainer
    {
    public:
        /// @brief Seeded from Havok's reflection, which names it hkRootLevelContainerNamedVariant (size 0x18).
        struct NamedVariant
        {
            hkStringPtr         m_name;        ///< +0x0
            hkStringPtr         m_className;   ///< +0x8
            hkReferencedObject* m_variant;     ///< +0x10
        };

        hkArray<NamedVariant> m_namedVariants;   ///< +0x0
    };
} }
