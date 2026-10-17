#pragma once

/// @file
/// @brief Layouts seeded from Havok's own reflection in the exe (tools/cbmatch --seed-headers): Havok's member names,
///        the gaps as explicit padding; tools/cbmatch --check-layout proves every named member.

#include <Common/Base/hkBase.h>
#include <Physics/Dynamics/Constraint/Atom/hkpConstraintAtom.h>
#include <Physics/Dynamics/Constraint/hkpConstraintData.h>

namespace CB { namespace schema
{
    /// @brief Seeded from Havok's reflection (size 0xb0).
    class hkpHingeLimitsData : public hkpConstraintData
    {
    public:
        /// @brief Seeded from Havok's reflection (size 0x90).
        struct Atoms
        {
        public:
            enum Axis
            {
                AXIS_AXLE = 0,
                AXIS_PERP_TO_AXLE_1 = 1,
                AXIS_PERP_TO_AXLE_2 = 2,
            };
            hkpSetLocalRotationsConstraintAtom m_rotations;   ///< +0x0
            hkpAngLimitConstraintAtom m_angLimit;   ///< +0x70
            hkp2dAngConstraintAtom m_2dAng;   ///< +0x80
        };

        Atoms m_atoms;   ///< +0x20
    };
} }
