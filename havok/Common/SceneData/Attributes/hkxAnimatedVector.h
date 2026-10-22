#pragma once

/// @file
/// @brief Layouts seeded from Havok's own reflection in the exe (tools/cbmatch --seed-headers): Havok's member names,
///        the gaps as explicit padding; tools/cbmatch --check-layout proves every named member.

#include <Common/Base/Object/hkReferencedObject.h>
#include <Common/Base/hkBase.h>

namespace CB { namespace havok
{
    /// @brief Seeded from Havok's reflection (size 0x28, version 1).
    class hkxAnimatedVector : public hkReferencedObject
    {
    public:
        hkArray<hkVector4> m_vectors;   ///< +0x10
        hkUint8 m_hint;   ///< +0x20
    };
} }
