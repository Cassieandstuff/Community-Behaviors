#pragma once

/// @file
/// @brief Layouts seeded from Havok's own reflection in the exe (tools/cbmatch --seed-headers): Havok's member names,
///        the gaps as explicit padding; tools/cbmatch --check-layout proves every named member.

#include <Common/Base/hkBase.h>
#include <Physics/Dynamics/Action/hkpUnaryAction.h>

namespace CB { namespace havok
{
    /// @brief Seeded from Havok's reflection (size 0x60).
    class hkpMotorAction : public hkpUnaryAction
    {
    public:
        hkVector4 m_axis;   ///< +0x40
        hkReal m_spinRate;   ///< +0x50
        hkReal m_gain;   ///< +0x54
        hkBool m_active;   ///< +0x58
    };
} }
