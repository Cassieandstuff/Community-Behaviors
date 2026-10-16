/// @file
/// @brief hkbCharacterControllerSceneModifier: the scene pass that moves every character's controller and steps the
///        physics world for them.
#pragma once

#include <Behavior/World/hkbSceneModifier.h>

namespace CB { namespace schema
{
    /// @note 0x28 bytes. The destructor is implicit (vanilla's scalar deleting destructor stamps only hkBaseObject's
    ///       vtable).
    class hkbCharacterControllerSceneModifier : public hkbSceneModifier
    {
    public:
        /// @note Not owned; names provisional (hkbWorld hands the modifier its physics world).
        hkpWorld*               m_physicsWorld;     ///< +0x10
        hkbPhysicsWorldStepper* m_physicsStepper;   ///< +0x18, null = hkpWorld::stepDeltaTime
        /// @brief Step the world only when some controller asks for it.
        /// @note Name provisional.
        hkBool                  m_stepOnlyWhenNeeded;   ///< +0x20

        hkbCharacterControllerSceneModifier();

        /// @brief Runs each controller's pre-step, steps the world (when time passes, and some controller needs it or
        ///        m_stepOnlyWhenNeeded is clear) and marks it stepped, then runs each controller's post-step.
        virtual void modify(hkbSceneCharacters& sceneCharacters, hkReal timestep);   ///< slot 3
    };
} }
