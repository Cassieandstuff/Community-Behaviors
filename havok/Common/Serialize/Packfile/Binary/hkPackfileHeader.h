#pragma once

/// @file
/// @brief Layouts seeded from Havok's own reflection in the exe (tools/cbmatch --seed-headers): Havok's member names,
///        the gaps as explicit padding; tools/cbmatch --check-layout proves every named member.

#include <Common/Base/hkBase.h>

namespace CB { namespace havok
{
    /// @brief Seeded from Havok's reflection (size 0x40, version 1).
    struct hkPackfileHeader
    {
    public:
        hkInt32 m_magic[2];   ///< +0x0
        hkInt32 m_userTag;   ///< +0x8
        hkInt32 m_fileVersion;   ///< +0xc
        hkUint8 m_layoutRules[4];   ///< +0x10
        hkInt32 m_numSections;   ///< +0x14
        hkInt32 m_contentsSectionIndex;   ///< +0x18
        hkInt32 m_contentsSectionOffset;   ///< +0x1c
        hkInt32 m_contentsClassNameSectionIndex;   ///< +0x20
        hkInt32 m_contentsClassNameSectionOffset;   ///< +0x24
        char m_contentsVersion[16];   ///< +0x28
        hkInt32 m_flags;   ///< +0x38
        hkInt32 m_pad[1];   ///< +0x3c
    };
} }
