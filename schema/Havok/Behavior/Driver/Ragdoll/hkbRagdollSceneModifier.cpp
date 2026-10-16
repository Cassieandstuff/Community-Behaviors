/// @file
/// @brief hkbRagdollSceneModifier, reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/Driver/Ragdoll/hkbRagdollSceneModifier.h>

#include <Behavior/Character/hkbCharacter.h>
#include <Behavior/Driver/Ragdoll/hkbRagdollDriver.h>
#include <Physics/Dynamics/World/hkpWorld.h>

namespace CB { namespace schema
{
    hkbRagdollSceneModifier::hkbRagdollSceneModifier()
    :   m_physicsWorld(HK_NULL),
        m_physicsStepper(HK_NULL)
    {
    }

    void hkbRagdollSceneModifier::modify(hkbSceneCharacters& sceneCharacters, hkReal timestep)
    {
        if (!m_physicsWorld) return;

        for (hkPointerMap<hkbCharacter*, hkbSceneCharacters::CharacterInfo*>::Iterator it = sceneCharacters.m_characters.getIterator();
             sceneCharacters.m_characters.isValid(it); it = sceneCharacters.m_characters.getNext(it)) {
            hkbSceneCharacters::CharacterInfo* info      = sceneCharacters.m_characters.getValue(it);
            hkbCharacter*                      character = sceneCharacters.m_characters.getKey(it);
            hkbRagdollDriver*                  driver    = character->m_ragdollDriver;
            if (driver) driver->driveToPose(timestep, *info->m_context, *info->m_generatorOutput);
        }

        if (!sceneCharacters.m_physicsWorldStepped && timestep > 0.0f) {
            if (!m_physicsStepper) {
                m_physicsWorld->stepDeltaTime(timestep);
            }
            else {
                m_physicsStepper->stepPhysicsWorld(m_physicsWorld, timestep);
            }
        }

        for (hkPointerMap<hkbCharacter*, hkbSceneCharacters::CharacterInfo*>::Iterator it = sceneCharacters.m_characters.getIterator();
             sceneCharacters.m_characters.isValid(it); it = sceneCharacters.m_characters.getNext(it)) {
            hkbSceneCharacters::CharacterInfo* info      = sceneCharacters.m_characters.getValue(it);
            hkbCharacter*                      character = sceneCharacters.m_characters.getKey(it);
            hkbRagdollDriver*                  driver    = character->m_ragdollDriver;
            if (driver) driver->postPhysics(*info->m_context, *info->m_generatorOutput);
        }
    }
} }
