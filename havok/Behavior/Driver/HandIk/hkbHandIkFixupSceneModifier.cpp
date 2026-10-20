/// @file
/// @brief hkbHandIkFixupSceneModifier, reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/Driver/HandIk/hkbHandIkFixupSceneModifier.h>

#include <Behavior/Character/hkbCharacter.h>
#include <Behavior/Driver/HandIk/hkbHandIkDriver.h>

namespace CB { namespace havok
{
    hkbHandIkFixupSceneModifier::hkbHandIkFixupSceneModifier()
    {
    }

    void hkbHandIkFixupSceneModifier::modify(hkbSceneCharacters& sceneCharacters, hkReal timestep)
    {
        for (hkPointerMap<hkbCharacter*, hkbSceneCharacters::CharacterInfo*>::Iterator it = sceneCharacters.m_characters.getIterator();
             sceneCharacters.m_characters.isValid(it); it = sceneCharacters.m_characters.getNext(it)) {
            hkbCharacter*                      character = sceneCharacters.m_characters.getKey(it);
            hkbSceneCharacters::CharacterInfo* info      = sceneCharacters.m_characters.getValue(it);
            hkbHandIkDriver*                   driver    = character->m_handIkDriver;
            if (driver) driver->drive(*info->m_context, *info->m_generatorOutput, timestep, true);
        }
    }
} }
