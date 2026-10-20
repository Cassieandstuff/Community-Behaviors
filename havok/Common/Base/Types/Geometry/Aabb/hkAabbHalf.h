#pragma once

/// @file
/// @brief Layouts seeded from Havok's own reflection in the exe (tools/cbmatch --seed-headers): Havok's member names,
///        the gaps as explicit padding; tools/cbmatch --check-layout proves every named member.

#include <Common/Base/hkBase.h>

namespace CB { namespace havok
{
    /// @brief Seeded from Havok's reflection (size 0x10).
    struct hkAabbHalf
    {
    public:
        hkUint16 m_data[6];   ///< +0x0
        hkUint16 m_extras[2];   ///< +0xc
    };
} }
