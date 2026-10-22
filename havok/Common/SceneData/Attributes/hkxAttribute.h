#pragma once

/// @file
/// @brief Layouts seeded from Havok's own reflection in the exe (tools/cbmatch --seed-headers): Havok's member names,
///        the gaps as explicit padding; tools/cbmatch --check-layout proves every named member.

#include <Common/Base/Object/hkReferencedObject.h>
#include <Common/Base/hkBase.h>

namespace CB { namespace havok
{
    /// @brief Seeded from Havok's reflection (size 0x10).
    struct hkxAttribute
    {
    public:
        enum Hint
        {
            HINT_NONE = 0,
            HINT_IGNORE = 1,
            HINT_TRANSFORM = 2,
            HINT_SCALE = 4,
            HINT_TRANSFORM_AND_SCALE = 6,
            HINT_FLIP = 8,
        };
        hkStringPtr m_name;   ///< +0x0
        hkReferencedObject* m_value;   ///< +0x8
    };
} }
