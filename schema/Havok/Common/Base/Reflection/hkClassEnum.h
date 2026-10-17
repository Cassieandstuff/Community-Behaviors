#pragma once

/// @file
/// @brief Layouts seeded from Havok's own reflection in the exe (tools/cbmatch --seed-headers): Havok's member names,
///        the gaps as explicit padding; tools/cbmatch --check-layout proves every named member.

#include <Common/Base/hkBase.h>

namespace CB { namespace schema
{
    class hkCustomAttributes;

    /// @brief Seeded from Havok's reflection (size 0x28).
    class hkClassEnum
    {
    public:
        /// @brief Seeded from Havok's reflection (size 0x10).
        struct Item
        {
        public:
            hkInt32 m_value;   ///< +0x0
            const char* m_name;   ///< +0x8
        };

        enum FlagValues
        {
            FLAGS_NONE = 0,
        };
        const char* m_name;   ///< +0x0
        hkUlong m_pad0x8[2];   ///< items (SIMPLEARRAY, a type the schema lacks)
        hkCustomAttributes* m_attributes;   ///< +0x18, not serialized
        hkUint32 m_flags;   ///< +0x20
    };
} }
