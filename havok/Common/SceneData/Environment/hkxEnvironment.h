#pragma once

/// @file
/// @brief Layouts seeded from Havok's own reflection in the exe (tools/cbmatch --seed-headers): Havok's member names,
///        the gaps as explicit padding; tools/cbmatch --check-layout proves every named member.

#include <Common/Base/Object/hkReferencedObject.h>
#include <Common/Base/hkBase.h>

namespace CB { namespace havok
{
    /// @brief Seeded from Havok's reflection (size 0x20, version 1).
    class hkxEnvironment : public hkReferencedObject
    {
    public:
        /// @brief Seeded from Havok's reflection (size 0x10).
        struct Variable
        {
        public:
            hkStringPtr m_name;   ///< +0x0
            hkStringPtr m_value;   ///< +0x8
        };

        hkArray<Variable> m_variables;   ///< +0x10
    };
} }
