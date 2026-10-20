#pragma once

/// @file
/// @brief Layouts seeded from Havok's own reflection in the exe (tools/cbmatch --seed-headers): Havok's member names,
///        the gaps as explicit padding; tools/cbmatch --check-layout proves every named member.

#include <Common/Base/hkBase.h>
#include <Physics/Dynamics/Constraint/Atom/Bridge/hkpBridgeConstraintAtom.h>
#include <Physics/Dynamics/Constraint/Generic/hkpGenericConstraintDataScheme.h>
#include <Physics/Dynamics/Constraint/hkpConstraintData.h>

namespace CB { namespace havok
{
    /// @brief Seeded from Havok's reflection (size 0x80).
    class hkpGenericConstraintData : public hkpConstraintData
    {
    public:
        hkpBridgeAtoms m_atoms;   ///< +0x18
        hkpGenericConstraintDataScheme m_scheme;   ///< +0x30
    };
} }
