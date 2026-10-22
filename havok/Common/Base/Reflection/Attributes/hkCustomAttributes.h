#pragma once

/// @file
/// @brief Layouts seeded from Havok's own reflection in the exe (tools/cbmatch --seed-headers): Havok's member names,
///        the gaps as explicit padding; tools/cbmatch --check-layout proves every named member.

#include <Common/Base/hkBase.h>

namespace CB { namespace havok
{
    /// @brief Seeded from Havok's reflection (size 0x10).
    class hkCustomAttributes
    {
    public:
        /// @brief Seeded from Havok's reflection (size 0x18).
        struct Attribute
        {
        public:
            const char* m_name;   ///< +0x0
            hkUlong m_pad0x8[2];   ///< value (VARIANT, a type the schema lacks)
        };

        hkUlong m_pad0x0[2];   ///< attributes (SIMPLEARRAY, a type the schema lacks)
    };
} }
