#pragma once

/// @file
/// @brief Layouts seeded from Havok's own reflection in the exe (tools/cbmatch --seed-headers): Havok's member names,
///        the gaps as explicit padding; tools/cbmatch --check-layout proves every named member.

#include <Common/Base/Object/hkReferencedObject.h>
#include <Common/Base/hkBase.h>

namespace CB { namespace schema
{
    /// @brief Seeded from Havok's reflection (size 0x20).
    class hkxEnum : public hkReferencedObject
    {
    public:
        /// @brief Seeded from Havok's reflection (size 0x10).
        struct Item
        {
        public:
            hkInt32 m_value;   ///< +0x0
            hkStringPtr m_name;   ///< +0x8
        };

        hkArray<Item> m_items;   ///< +0x10
    };
} }
