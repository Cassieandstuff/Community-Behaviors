#pragma once

/// @file
/// @brief Layouts seeded from Havok's own reflection in the exe (tools/cbmatch --seed-headers): Havok's member names,
///        the gaps as explicit padding; tools/cbmatch --check-layout proves every named member.

#include <Common/Base/hkBase.h>
#include <Physics/Dynamics/Constraint/Atom/Bridge/hkpBridgeConstraintAtom.h>
#include <Physics/Dynamics/Constraint/hkpConstraintData.h>

namespace CB { namespace havok
{
    /// @brief Seeded from Havok's reflection (size 0x48).
    class hkpBreakableConstraintData : public hkpConstraintData
    {
    public:
        hkpBridgeAtoms m_atoms;   ///< +0x18
        hkpConstraintData* m_constraintData;   ///< +0x30
        hkUint16 m_childRuntimeSize;   ///< +0x38
        hkUint16 m_childNumSolverResults;   ///< +0x3a
        hkReal m_solverResultLimit;   ///< +0x3c
        hkBool m_removeWhenBroken;   ///< +0x40
        hkBool m_revertBackVelocityOnBreak;   ///< +0x41
    };
} }
