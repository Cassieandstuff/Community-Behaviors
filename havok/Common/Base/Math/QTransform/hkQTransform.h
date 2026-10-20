#pragma once

/// @file
/// @brief Layouts seeded from Havok's own reflection in the exe (tools/cbmatch --seed-headers): Havok's member names,
///        the gaps as explicit padding; tools/cbmatch --check-layout proves every named member.

#include <Common/Base/hkBase.h>

namespace CB { namespace havok
{
    /// @brief Seeded from Havok's reflection (size 0x20).
    struct hkQTransform
    {
    public:
        hkQuaternion m_rotation;   ///< +0x0
        hkVector4 m_translation;   ///< +0x10
    };
} }
