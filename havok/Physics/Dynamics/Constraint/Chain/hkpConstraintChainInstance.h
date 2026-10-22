#pragma once

/// @file
/// @brief Layouts seeded from Havok's own reflection in the exe (tools/cbmatch --seed-headers): Havok's member names,
///        the gaps as explicit padding; tools/cbmatch --check-layout proves every named member.

#include <Common/Base/hkBase.h>
#include <Physics/Dynamics/Constraint/hkpConstraintInstance.h>
#include <Physics/Dynamics/Entity/hkpEntity.h>

namespace CB { namespace havok
{
    class hkpConstraintChainInstanceAction;

    /// @brief Seeded from Havok's reflection (size 0x88).
    class hkpConstraintChainInstance : public hkpConstraintInstance
    {
    public:
        hkArray<hkpEntity*> m_chainedEntities;   ///< +0x70
        hkpConstraintChainInstanceAction* m_action;   ///< +0x80
    };
} }
