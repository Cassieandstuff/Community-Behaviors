#pragma once

/// @file
/// @brief Layouts seeded from Havok's own reflection in the exe (tools/cbmatch --seed-headers): Havok's member names,
///        the gaps as explicit padding; tools/cbmatch --check-layout proves every named member.

#include <Common/Base/hkBase.h>
#include <Physics/Dynamics/Action/hkpUnaryAction.h>

namespace CB { namespace havok
{
    /// @brief Seeded from Havok's reflection (size 0x70).
    class hkpReorientAction : public hkpUnaryAction
    {
    public:
        hkVector4 m_rotationAxis;   ///< +0x40
        hkVector4 m_upAxis;   ///< +0x50
        hkReal m_strength;   ///< +0x60
        hkReal m_damping;   ///< +0x64
    };
} }
