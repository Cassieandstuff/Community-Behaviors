/// @file
/// @brief hkbWorld, reimplemented against vanilla (AE 1.6.1170): the functions matched so far.
#include <Behavior/World/hkbWorld.h>

#include <Animation/Ik/FootPlacement/hkaRaycastInterface.h>
#include <Behavior/Attachment/hkbAttachmentManager.h>
#include <Behavior/Character/hkbCharacter.h>
#include <Behavior/World/hkbSceneModifier.h>

namespace CB { namespace schema
{
    hkbWorld::~hkbWorld()
    {
        for (int i = m_characters.getSize() - 1; i >= 0; i--) {
            removeCharacter(m_characters[i], true);
        }
        for (int i = m_listeners.getSize() - 1; i >= 0; i--) {
            m_listeners[i]->worldDeletedCallback(this);
        }
        delete m_attachmentManager;
        delete m_raycastInterface;
        delete m_lockedArray;
    }

    /// @note The removal is hkArray::removeAt spelled out with a one-qword memCpy8: vanilla addresses the moved element
    ///       through two lea'd pointers, which removeAt's memCpyOne<8> folds into indexed operands under VS2015.
    void hkbWorld::removeCharacter(hkbCharacter* character, bool deactivate)
    {
        const int index = m_characters.indexOf(character);
        m_characters.m_size--;
        if (m_characters.m_size != index) hkMemUtil::memCpy8(m_characters.m_data + index, m_characters.m_data + m_characters.m_size, 8);
        if (deactivate) character->deactivate();
        for (int i = 0; i < m_listeners.getSize(); i++) {
            m_listeners[i]->characterRemovedCallback(character);
        }
        character->m_world = HK_NULL;
        character->removeReference();
    }
} }
