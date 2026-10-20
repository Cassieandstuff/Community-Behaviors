#pragma once

/// @file
/// @brief Layouts seeded from Havok's own reflection in the exe (tools/cbmatch --seed-headers): Havok's member names,
///        the gaps as explicit padding; tools/cbmatch --check-layout proves every named member.

#include <Common/Base/hkBase.h>
#include <Physics/Dynamics/Constraint/Atom/Bridge/hkpBridgeConstraintAtom.h>
#include <Physics/Dynamics/Constraint/Chain/hkpConstraintChainData.h>
#include <Physics/Dynamics/Constraint/Motor/hkpConstraintMotor.h>

namespace CB { namespace havok
{
    /// @brief Seeded from Havok's reflection (size 0x60).
    class hkpPoweredChainData : public hkpConstraintChainData
    {
    public:
        /// @brief Seeded from Havok's reflection (size 0x60).
        struct ConstraintInfo
        {
        public:
            hkVector4 m_pivotInA;   ///< +0x0
            hkVector4 m_pivotInB;   ///< +0x10
            hkQuaternion m_aTc;   ///< +0x20
            hkQuaternion m_bTc;   ///< +0x30
            hkpConstraintMotor* m_motors[3];   ///< +0x40
            hkBool m_switchBodies;   ///< +0x58
        };

        hkpBridgeAtoms m_atoms;   ///< +0x18
        hkArray<ConstraintInfo> m_infos;   ///< +0x30
        hkReal m_tau;   ///< +0x40
        hkReal m_damping;   ///< +0x44
        hkReal m_cfmLinAdd;   ///< +0x48
        hkReal m_cfmLinMul;   ///< +0x4c
        hkReal m_cfmAngAdd;   ///< +0x50
        hkReal m_cfmAngMul;   ///< +0x54
        hkReal m_maxErrorDistance;   ///< +0x58
    };
} }
