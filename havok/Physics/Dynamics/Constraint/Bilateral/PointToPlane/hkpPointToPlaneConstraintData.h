#pragma once

/// @file
/// @brief Layouts seeded from Havok's own reflection in the exe (tools/cbmatch --seed-headers): Havok's member names,
///        the gaps as explicit padding; tools/cbmatch --check-layout proves every named member.

#include <Common/Base/hkBase.h>
#include <Physics/Dynamics/Constraint/Atom/hkpConstraintAtom.h>
#include <Physics/Dynamics/Constraint/hkpConstraintData.h>

namespace CB { namespace havok
{
    /// @brief Seeded from Havok's reflection (size 0xc0).
    class hkpPointToPlaneConstraintData : public hkpConstraintData
    {
    public:
        /// @brief Seeded from Havok's reflection (size 0xa0).
        struct Atoms
        {
        public:
            hkpSetLocalTransformsConstraintAtom m_transforms;   ///< +0x0
            hkpLinConstraintAtom m_lin;   ///< +0x90
        };

        Atoms m_atoms;   ///< +0x20
    };
} }
