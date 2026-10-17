#pragma once

/// @file
/// @brief Layouts seeded from Havok's own reflection in the exe (tools/cbmatch --seed-headers): Havok's member names,
///        the gaps as explicit padding; tools/cbmatch --check-layout proves every named member.

#include <Common/Base/hkBase.h>
#include <Physics/Dynamics/Constraint/Atom/hkpConstraintAtom.h>
#include <Physics/Dynamics/Constraint/hkpConstraintData.h>

namespace CB { namespace schema
{
    /// @brief Seeded from Havok's reflection (size 0xf0).
    class hkpPrismaticConstraintData : public hkpConstraintData
    {
    public:
        /// @brief Seeded from Havok's reflection (size 0xd0).
        struct Atoms
        {
        public:
            enum Axis
            {
                AXIS_SHAFT = 0,
                AXIS_PERP_TO_SHAFT = 1,
            };
            hkpSetLocalTransformsConstraintAtom m_transforms;   ///< +0x0
            hkpLinMotorConstraintAtom m_motor;   ///< +0x90
            hkpLinFrictionConstraintAtom m_friction;   ///< +0xa8
            hkpAngConstraintAtom m_ang;   ///< +0xb0
            hkpLinConstraintAtom m_lin0;   ///< +0xb4
            hkpLinConstraintAtom m_lin1;   ///< +0xb8
            hkpLinLimitConstraintAtom m_linLimit;   ///< +0xbc
        };

        Atoms m_atoms;   ///< +0x20
    };
} }
