#pragma once

/// @file
/// @brief Layouts seeded from Havok's own reflection in the exe (tools/cbmatch --seed-headers): Havok's member names,
///        the gaps as explicit padding; tools/cbmatch --check-layout proves every named member.

#include <Common/Base/hkBase.h>
#include <Physics/Dynamics/Constraint/Motor/hkpConstraintMotor.h>

namespace CB { namespace schema
{
    /// @brief Seeded from Havok's reflection (size 0x20).
    class hkpLimitedForceConstraintMotor : public hkpConstraintMotor
    {
    public:
        hkReal m_minForce;   ///< +0x18
        hkReal m_maxForce;   ///< +0x1c
    };
} }
