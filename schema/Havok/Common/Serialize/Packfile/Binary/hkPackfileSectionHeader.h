#pragma once

/// @file
/// @brief Layouts seeded from Havok's own reflection in the exe (tools/cbmatch --seed-headers): Havok's member names,
///        the gaps as explicit padding; tools/cbmatch --check-layout proves every named member.

#include <Common/Base/hkBase.h>

namespace CB { namespace schema
{
    /// @brief Seeded from Havok's reflection (size 0x30).
    struct hkPackfileSectionHeader
    {
    public:
        char m_sectionTag[19];   ///< +0x0
        char m_nullByte;   ///< +0x13
        hkInt32 m_absoluteDataStart;   ///< +0x14
        hkInt32 m_localFixupsOffset;   ///< +0x18
        hkInt32 m_globalFixupsOffset;   ///< +0x1c
        hkInt32 m_virtualFixupsOffset;   ///< +0x20
        hkInt32 m_exportsOffset;   ///< +0x24
        hkInt32 m_importsOffset;   ///< +0x28
        hkInt32 m_endOffset;   ///< +0x2c
    };
} }
