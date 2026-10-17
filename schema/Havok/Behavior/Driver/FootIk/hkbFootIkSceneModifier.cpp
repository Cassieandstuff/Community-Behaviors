/// @file
/// @brief hkbFootIkSceneModifier, reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/Driver/FootIk/hkbFootIkSceneModifier.h>

#include <Behavior/Character/hkbCharacter.h>
#include <Behavior/Driver/FootIk/hkbFootIkDriver.h>

namespace CB { namespace schema
{
    hkbFootIkSceneModifier::hkbFootIkSceneModifier()
    {
    }

    void hkbFootIkSceneModifier::modify(hkbSceneCharacters& sceneCharacters, hkReal timestep)
    {
        for (hkPointerMap<hkbCharacter*, hkbSceneCharacters::CharacterInfo*>::Iterator it = sceneCharacters.m_characters.getIterator();
             sceneCharacters.m_characters.isValid(it); it = sceneCharacters.m_characters.getNext(it)) {
            hkbCharacter*                      character = sceneCharacters.m_characters.getKey(it);
            hkbSceneCharacters::CharacterInfo* info      = sceneCharacters.m_characters.getValue(it);
            hkbFootIkDriver*                   driver    = character->m_footIkDriver;
            if (driver) driver->doFootIk(*info->m_context, *info->m_generatorOutput);
        }
    }
} }
