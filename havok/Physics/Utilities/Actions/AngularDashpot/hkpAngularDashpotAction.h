#pragma once

/// @file
/// @brief Layouts seeded from Havok's own reflection in the exe (tools/cbmatch --seed-headers): Havok's member names,
///        the gaps as explicit padding; tools/cbmatch --check-layout proves every named member.

#include <Common/Base/hkBase.h>
#include <Physics/Dynamics/Action/hkpBinaryAction.h>

namespace CB { namespace havok
{
    /// @brief Seeded from Havok's reflection (size 0x60).
    class hkpAngularDashpotAction : public hkpBinaryAction
    {
    public:
        hkQuaternion m_rotation;   ///< +0x40
        hkReal m_strength;   ///< +0x50
        hkReal m_damping;   ///< +0x54
    };
} }
