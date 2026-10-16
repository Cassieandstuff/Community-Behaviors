/// @file
/// @brief hkbCharacterControllerSceneModifier, reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/Driver/CharacterController/hkbCharacterControllerSceneModifier.h>

#include <Behavior/Character/hkbCharacter.h>
#include <Behavior/Driver/CharacterController/hkbCharacterControllerDriver.h>
#include <Physics/Dynamics/World/hkpWorld.h>

namespace CB { namespace schema
{
    hkbCharacterControllerSceneModifier::hkbCharacterControllerSceneModifier()
    :   m_physicsWorld(HK_NULL),
        m_physicsStepper(HK_NULL),
        m_stepOnlyWhenNeeded(false)
    {
    }

    void hkbCharacterControllerSceneModifier::modify(hkbSceneCharacters& sceneCharacters, hkReal timestep)
    {
        if (!m_physicsWorld || timestep <= 0.0f) return;

        bool needsPhysicsStep = false;
        for (hkPointerMap<hkbCharacter*, hkbSceneCharacters::CharacterInfo*>::Iterator it = sceneCharacters.m_characters.getIterator();
             sceneCharacters.m_characters.isValid(it); it = sceneCharacters.m_characters.getNext(it)) {
            hkbSceneCharacters::CharacterInfo* info      = sceneCharacters.m_characters.getValue(it);
            hkbCharacter*                      character = sceneCharacters.m_characters.getKey(it);
            hkbCharacterControllerDriver*      driver    = character->m_characterControllerDriver;
            if (driver) {
                driver->prePhysicsStep(timestep, *info->m_context, *info->m_generatorOutput);
                needsPhysicsStep |= driver->needsPhysicsStep();
            }
        }

        if (needsPhysicsStep || !m_stepOnlyWhenNeeded) {
            if (!m_physicsStepper) {
                m_physicsWorld->stepDeltaTime(timestep);
            }
            else {
                m_physicsStepper->stepPhysicsWorld(m_physicsWorld, timestep);
            }
            sceneCharacters.m_physicsWorldStepped = true;
        }

        for (hkPointerMap<hkbCharacter*, hkbSceneCharacters::CharacterInfo*>::Iterator it = sceneCharacters.m_characters.getIterator();
             sceneCharacters.m_characters.isValid(it); it = sceneCharacters.m_characters.getNext(it)) {
            hkbSceneCharacters::CharacterInfo* info      = sceneCharacters.m_characters.getValue(it);
            hkbCharacter*                      character = sceneCharacters.m_characters.getKey(it);
            hkbCharacterControllerDriver*      driver    = character->m_characterControllerDriver;
            if (driver) driver->postPhysicsStep(timestep, *info->m_context, *info->m_generatorOutput);
        }
    }
} }
