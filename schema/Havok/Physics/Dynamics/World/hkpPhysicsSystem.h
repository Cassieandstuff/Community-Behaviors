#pragma once

/// @file
/// @brief Layouts seeded from Havok's own reflection in the exe (tools/cbmatch --seed-headers): Havok's member names,
///        the gaps as explicit padding; tools/cbmatch --check-layout proves every named member.

#include <Common/Base/Object/hkReferencedObject.h>
#include <Common/Base/hkBase.h>
#include <Physics/Dynamics/Constraint/hkpConstraintInstance.h>
#include <Physics/Dynamics/Entity/hkpRigidBody.h>
#include <Physics/Dynamics/Phantom/hkpPhantom.h>

namespace CB { namespace schema
{
    class hkpAction;

    /// @brief Seeded from Havok's reflection (size 0x68).
    class hkpPhysicsSystem : public hkReferencedObject
    {
    public:
        hkArray<hkpRigidBody*> m_rigidBodies;   ///< +0x10
        hkArray<hkpConstraintInstance*> m_constraints;   ///< +0x20
        hkArray<hkpAction*> m_actions;   ///< +0x30
        hkArray<hkpPhantom*> m_phantoms;   ///< +0x40
        hkStringPtr m_name;   ///< +0x50
        hkUlong m_userData;   ///< +0x58
        hkBool m_active;   ///< +0x60
    };
} }
