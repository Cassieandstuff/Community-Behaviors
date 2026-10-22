#pragma once

/// @file
/// @brief Layouts seeded from Havok's own reflection in the exe (tools/cbmatch --seed-headers): Havok's member names,
///        the gaps as explicit padding; tools/cbmatch --check-layout proves every named member.

#include <Common/Base/hkBase.h>
#include <Physics/Dynamics/Constraint/Atom/hkpConstraintAtom.h>
#include <Physics/Dynamics/Constraint/hkpConstraintData.h>

namespace CB { namespace havok
{
    /// @brief Seeded from Havok's reflection (size 0x170).
    class hkpWheelConstraintData : public hkpConstraintData
    {
    public:
        /// @brief Seeded from Havok's reflection (size 0x130).
        struct Atoms
        {
        public:
            enum Axis
            {
                AXIS_SUSPENSION = 0,
                AXIS_PERP_SUSPENSION = 1,
                AXIS_AXLE = 0,
                AXIS_STEERING = 1,
            };
            hkpSetLocalTransformsConstraintAtom m_suspensionBase;   ///< +0x0
            hkpLinLimitConstraintAtom m_lin0Limit;   ///< +0x90
            hkpLinSoftConstraintAtom m_lin0Soft;   ///< +0x9c
            hkpLinConstraintAtom m_lin1;   ///< +0xa8
            hkpLinConstraintAtom m_lin2;   ///< +0xac
            hkpSetLocalRotationsConstraintAtom m_steeringBase;   ///< +0xb0
            hkp2dAngConstraintAtom m_2dAng;   ///< +0x120
        };

        Atoms m_atoms;   ///< +0x20
        hkVector4 m_initialAxleInB;   ///< +0x150
        hkVector4 m_initialSteeringAxisInB;   ///< +0x160
    };
} }
