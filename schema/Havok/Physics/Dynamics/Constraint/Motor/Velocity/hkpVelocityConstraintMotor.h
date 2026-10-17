#pragma once

/// @file
/// @brief Layouts seeded from Havok's own reflection in the exe (tools/cbmatch --seed-headers): Havok's member names,
///        the gaps as explicit padding; tools/cbmatch --check-layout proves every named member.

#include <Common/Base/hkBase.h>
#include <Physics/Dynamics/Constraint/Motor/LimitedForce/hkpLimitedForceConstraintMotor.h>

namespace CB { namespace schema
{
    /// @brief Seeded from Havok's reflection (size 0x30).
    class hkpVelocityConstraintMotor : public hkpLimitedForceConstraintMotor
    {
    public:
        hkReal m_tau;   ///< +0x20
        hkReal m_velocityTarget;   ///< +0x24
        hkBool m_useVelocityTargetFromConstraintTargets;   ///< +0x28
    };
} }
