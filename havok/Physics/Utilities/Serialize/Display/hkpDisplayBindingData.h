#pragma once

/// @file
/// @brief Layouts seeded from Havok's own reflection in the exe (tools/cbmatch --seed-headers): Havok's member names,
///        the gaps as explicit padding; tools/cbmatch --check-layout proves every named member.

#include <Common/Base/Object/hkReferencedObject.h>
#include <Common/Base/hkBase.h>
#include <Physics/Dynamics/Entity/hkpRigidBody.h>
#include <Physics/Dynamics/World/hkpPhysicsSystem.h>

namespace CB { namespace havok
{
    /// @brief Seeded from Havok's reflection (size 0x30, version 1).
    class hkpDisplayBindingData : public hkReferencedObject
    {
    public:
        /// @brief Seeded from Havok's reflection (size 0x60, version 2).
        class RigidBody : public hkReferencedObject
        {
        public:
            hkpRigidBody* m_rigidBody;   ///< +0x10
            hkReferencedObject* m_displayObjectPtr;   ///< +0x18
            hkMatrix4 m_rigidBodyFromDisplayObjectTransform;   ///< +0x20
        };

        /// @brief Seeded from Havok's reflection (size 0x28, version 1).
        class PhysicsSystem : public hkReferencedObject
        {
        public:
            hkArray<RigidBody*> m_bindings;   ///< +0x10
            hkpPhysicsSystem* m_system;   ///< +0x20
        };

        hkArray<RigidBody*> m_rigidBodyBindings;   ///< +0x10
        hkArray<PhysicsSystem*> m_physicsSystemBindings;   ///< +0x20
    };
} }
