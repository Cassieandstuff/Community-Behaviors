#pragma once

/// @file
/// @brief Layouts seeded from Havok's own reflection in the exe (tools/cbmatch --seed-headers): Havok's member names,
///        the gaps as explicit padding; tools/cbmatch --check-layout proves every named member.

#include <Common/Base/hkBase.h>
#include <Physics/Dynamics/Constraint/Atom/Bridge/hkpBridgeConstraintAtom.h>
#include <Physics/Dynamics/Constraint/Chain/hkpConstraintChainData.h>

namespace CB { namespace schema
{
    /// @brief Seeded from Havok's reflection (size 0x50).
    class hkpStiffSpringChainData : public hkpConstraintChainData
    {
    public:
        /// @brief Seeded from Havok's reflection (size 0x30).
        struct ConstraintInfo
        {
        public:
            hkVector4 m_pivotInA;   ///< +0x0
            hkVector4 m_pivotInB;   ///< +0x10
            hkReal m_springLength;   ///< +0x20
        };

        hkpBridgeAtoms m_atoms;   ///< +0x18
        hkArray<ConstraintInfo> m_infos;   ///< +0x30
        hkReal m_tau;   ///< +0x40
        hkReal m_damping;   ///< +0x44
        hkReal m_cfm;   ///< +0x48
    };
} }
