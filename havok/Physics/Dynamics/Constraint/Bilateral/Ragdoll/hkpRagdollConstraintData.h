#pragma once

/// @file
/// @brief Layouts seeded from Havok's own reflection in the exe (tools/cbmatch --seed-headers): Havok's member names,
///        the gaps as explicit padding; tools/cbmatch --check-layout proves every named member.

#include <Common/Base/hkBase.h>
#include <Physics/Dynamics/Constraint/Atom/hkpConstraintAtom.h>
#include <Physics/Dynamics/Constraint/hkpConstraintData.h>

namespace CB { namespace havok
{
    /// @brief Seeded from Havok's reflection (size 0x160, version 1).
    struct hkpRagdollConstraintDataAtoms
    {
    public:
        enum Axis
        {
            AXIS_TWIST = 0,
            AXIS_PLANES = 1,
            AXIS_CROSS_PRODUCT = 2,
        };
        hkpSetLocalTransformsConstraintAtom m_transforms;   ///< +0x0
        hkpSetupStabilizationAtom m_setupStabilization;   ///< +0x90
        hkpRagdollMotorConstraintAtom m_ragdollMotors;   ///< +0xa0
        hkpAngFrictionConstraintAtom m_angFriction;   ///< +0x100
        hkpTwistLimitConstraintAtom m_twistLimit;   ///< +0x10c
        hkpConeLimitConstraintAtom m_coneLimit;   ///< +0x120
        hkpConeLimitConstraintAtom m_planesLimit;   ///< +0x134
        hkpBallSocketConstraintAtom m_ballSocket;   ///< +0x148
    };

    /// @brief Seeded from Havok's reflection (size 0x180).
    class hkpRagdollConstraintData : public hkpConstraintData
    {
    public:
        enum MotorIndex
        {
            MOTOR_TWIST = 0,
            MOTOR_PLANE = 1,
            MOTOR_CONE = 2,
        };
        hkpRagdollConstraintDataAtoms m_atoms;   ///< +0x20
    };
} }
