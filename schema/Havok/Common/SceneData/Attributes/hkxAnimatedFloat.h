#pragma once

/// @file
/// @brief Layouts seeded from Havok's own reflection in the exe (tools/cbmatch --seed-headers): Havok's member names,
///        the gaps as explicit padding; tools/cbmatch --check-layout proves every named member.

#include <Common/Base/Object/hkReferencedObject.h>
#include <Common/Base/hkBase.h>

namespace CB { namespace schema
{
    /// @brief Seeded from Havok's reflection (size 0x28, version 1).
    class hkxAnimatedFloat : public hkReferencedObject
    {
    public:
        hkArray<hkReal> m_floats;   ///< +0x10
        hkUint8 m_hint;   ///< +0x20
    };
} }
