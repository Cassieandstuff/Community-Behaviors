#pragma once

/// @file
/// @brief Layouts seeded from Havok's own reflection in the exe (tools/cbmatch --seed-headers): Havok's member names,
///        the gaps as explicit padding; tools/cbmatch --check-layout proves every named member.

#include <Common/Base/hkBase.h>

namespace CB { namespace schema
{
    /// @brief Seeded from Havok's reflection (size 0x20).
    struct hkAabbUint32
    {
    public:
        hkUint32 m_min[3];   ///< +0x0
        hkUint8 m_expansionMin[3];   ///< +0xc
        hkUint8 m_expansionShift;   ///< +0xf
        hkUint32 m_max[3];   ///< +0x10
        hkUint8 m_expansionMax[3];   ///< +0x1c
        hkUint8 m_shapeKeyByte;   ///< +0x1f
    };
} }
