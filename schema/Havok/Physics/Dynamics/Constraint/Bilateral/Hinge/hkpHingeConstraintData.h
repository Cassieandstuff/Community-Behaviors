#pragma once

/// @file
/// @brief Layouts seeded from Havok's own reflection in the exe (tools/cbmatch --seed-headers): Havok's member names,
///        the gaps as explicit padding; tools/cbmatch --check-layout proves every named member.

#include <Common/Base/hkBase.h>
#include <Physics/Dynamics/Constraint/Atom/hkpConstraintAtom.h>
#include <Physics/Dynamics/Constraint/hkpConstraintData.h>

namespace CB { namespace schema
{
    /// @brief Seeded from Havok's reflection (size 0xe0).
    class hkpHingeConstraintData : public hkpConstraintData
    {
    public:
        /// @brief Seeded from Havok's reflection (size 0xc0, version 1).
        struct Atoms
        {
        public:
            enum Axis
            {
                AXIS_AXLE = 0,
            };
            hkpSetLocalTransformsConstraintAtom m_transforms;   ///< +0x0
            hkpSetupStabilizationAtom m_setupStabilization;   ///< +0x90
            hkp2dAngConstraintAtom m_2dAng;   ///< +0xa0
            hkpBallSocketConstraintAtom m_ballSocket;   ///< +0xa4
        };

        Atoms m_atoms;   ///< +0x20
    };
} }
