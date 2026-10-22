/// @file
/// @brief hkbAttachmentSceneModifier, reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/Attachment/hkbAttachmentSceneModifier.h>

namespace CB { namespace havok
{
    hkbAttachmentSceneModifier::hkbAttachmentSceneModifier()
    :   m_attachmentManager(HK_NULL)
    {
    }

    void hkbAttachmentSceneModifier::modify(hkbSceneCharacters& sceneCharacters, hkReal timestep)
    {
        if (m_attachmentManager && timestep > 0.0f && sceneCharacters.m_characters.getSize() > 0) {
            m_attachmentManager->update(timestep, sceneCharacters, false);
        }
    }
} }
