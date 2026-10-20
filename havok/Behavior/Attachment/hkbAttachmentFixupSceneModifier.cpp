/// @file
/// @brief hkbAttachmentFixupSceneModifier, reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/Attachment/hkbAttachmentFixupSceneModifier.h>

namespace CB { namespace havok
{
    hkbAttachmentFixupSceneModifier::hkbAttachmentFixupSceneModifier()
    :   m_attachmentManager(HK_NULL)
    {
    }

    void hkbAttachmentFixupSceneModifier::modify(hkbSceneCharacters& sceneCharacters, hkReal timestep)
    {
        if (m_attachmentManager && timestep > 0.0f) m_attachmentManager->update(timestep, sceneCharacters, true);
    }
} }
