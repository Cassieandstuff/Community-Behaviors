#pragma once

/// @file
/// @brief Layouts seeded from Havok's own reflection in the exe (tools/cbmatch --seed-headers): Havok's member names,
///        the gaps as explicit padding; tools/cbmatch --check-layout proves every named member.

#include <Common/Base/hkBase.h>

namespace CB { namespace schema
{
    /// @brief Seeded from Havok's reflection (size 0x1).
    struct hkColor
    {
    public:
        hkUint8 m_pad0x0[0x1];

        typedef hkUint32 Argb;

        /// @brief Packs the channels (each 0..1) as 0xAARRGGBB.
        /// @note Out of line in vanilla (0x140a8b050).
        static Argb rgbFromFloats(const hkReal red, const hkReal green, const hkReal blue, const hkReal alpha = 1.0f);
    };
} }
