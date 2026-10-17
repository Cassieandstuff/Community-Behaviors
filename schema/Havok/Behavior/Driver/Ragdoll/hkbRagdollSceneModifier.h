/// @file
/// @brief hkbRagdollSceneModifier: the scene pass that drives every character's ragdoll and steps the physics world.
#pragma once

#include <Behavior/World/hkbSceneModifier.h>

namespace CB { namespace schema
{
    /// @note 0x20 bytes. The destructor is implicit (vanilla's scalar deleting destructor stamps only hkBaseObject's
    ///       vtable).
    class hkbRagdollSceneModifier : public hkbSceneModifier
    {
    public:
        /// @note Not owned; names provisional (hkbWorld hands the modifier its physics world).
        hkpWorld*               m_physicsWorld;     ///< +0x10
        hkbPhysicsWorldStepper* m_physicsStepper;   ///< +0x18, null = hkpWorld::stepDeltaTime

        hkbRagdollSceneModifier();

        /// @brief Drives each ragdoll toward its character's pose, steps the world (unless the character controller pass
        ///        already did, or no time passes), then feeds each ragdoll back into its pose.
        virtual void modify(hkbSceneCharacters& sceneCharacters, hkReal timestep);   ///< slot 3
    };
} }
