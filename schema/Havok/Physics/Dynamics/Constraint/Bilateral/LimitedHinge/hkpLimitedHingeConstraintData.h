#pragma once

/// @file
/// @brief Layouts seeded from Havok's own reflection in the exe (tools/cbmatch --seed-headers): Havok's member names,
///        the gaps as explicit padding; tools/cbmatch --check-layout proves every named member.

#include <Common/Base/hkBase.h>
#include <Physics/Dynamics/Constraint/Atom/hkpConstraintAtom.h>
#include <Physics/Dynamics/Constraint/hkpConstraintData.h>

namespace CB { namespace schema
{
    /// @brief Seeded from Havok's reflection (size 0xf0, version 1).
    struct hkpLimitedHingeConstraintDataAtoms
    {
    public:
        enum Axis
        {
            AXIS_AXLE = 0,
            AXIS_PERP_TO_AXLE_1 = 1,
            AXIS_PERP_TO_AXLE_2 = 2,
        };
        hkpSetLocalTransformsConstraintAtom m_transforms;   ///< +0x0
        hkpSetupStabilizationAtom m_setupStabilization;   ///< +0x90
        hkpAngMotorConstraintAtom m_angMotor;   ///< +0xa0
        hkpAngFrictionConstraintAtom m_angFriction;   ///< +0xb8
        hkpAngLimitConstraintAtom m_angLimit;   ///< +0xc4
        hkp2dAngConstraintAtom m_2dAng;   ///< +0xd4
        hkpBallSocketConstraintAtom m_ballSocket;   ///< +0xd8
    };

    /// @brief Seeded from Havok's reflection (size 0x110).
    class hkpLimitedHingeConstraintData : public hkpConstraintData
    {
    public:
        hkpLimitedHingeConstraintDataAtoms m_atoms;   ///< +0x20
    };
} }
