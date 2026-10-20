#pragma once

/// @file
/// @brief Layouts seeded from Havok's own reflection in the exe (tools/cbmatch --seed-headers): Havok's member names,
///        the gaps as explicit padding; tools/cbmatch --check-layout proves every named member.

#include <Common/Base/hkBase.h>
#include <Physics/Dynamics/Constraint/Atom/hkpConstraintAtom.h>
#include <Physics/Dynamics/Constraint/hkpConstraintData.h>

namespace CB { namespace havok
{
    /// @brief Seeded from Havok's reflection (size 0xd0).
    class hkpRagdollLimitsData : public hkpConstraintData
    {
    public:
        /// @brief Seeded from Havok's reflection (size 0xb0).
        struct Atoms
        {
        public:
            enum Axis
            {
                AXIS_TWIST = 0,
                AXIS_PLANES = 1,
                AXIS_CROSS_PRODUCT = 2,
            };
            hkpSetLocalRotationsConstraintAtom m_rotations;   ///< +0x0
            hkpTwistLimitConstraintAtom m_twistLimit;   ///< +0x70
            hkpConeLimitConstraintAtom m_coneLimit;   ///< +0x84
            hkpConeLimitConstraintAtom m_planesLimit;   ///< +0x98
        };

        Atoms m_atoms;   ///< +0x20
    };
} }
