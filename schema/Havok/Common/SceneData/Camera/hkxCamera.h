#pragma once

/// @file
/// @brief Layouts seeded from Havok's own reflection in the exe (tools/cbmatch --seed-headers): Havok's member names,
///        the gaps as explicit padding; tools/cbmatch --check-layout proves every named member.

#include <Common/Base/Object/hkReferencedObject.h>
#include <Common/Base/hkBase.h>

namespace CB { namespace schema
{
    /// @brief Seeded from Havok's reflection (size 0x50, version 1).
    class hkxCamera : public hkReferencedObject
    {
    public:
        hkVector4 m_from;   ///< +0x10
        hkVector4 m_focus;   ///< +0x20
        hkVector4 m_up;   ///< +0x30
        hkReal m_fov;   ///< +0x40
        hkReal m_far;   ///< +0x44
        hkReal m_near;   ///< +0x48
        hkBool m_leftHanded;   ///< +0x4c
    };
} }
