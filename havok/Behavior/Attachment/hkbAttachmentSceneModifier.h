/// @file
/// @brief hkbAttachmentSceneModifier: the scene pass that advances the world's attachments.
#pragma once

#include <Behavior/Attachment/hkbAttachmentManager.h>
#include <Behavior/World/hkbSceneModifier.h>

namespace CB { namespace havok
{
    /// @note The destructor is implicit (vanilla's scalar deleting destructor stamps only hkBaseObject's vtable).
    class hkbAttachmentSceneModifier : public hkbSceneModifier
    {
    public:
        /// @note Not owned: hkbWorld sets it to its own manager after construction.
        hkbAttachmentManager* m_attachmentManager;   ///< +0x10

        hkbAttachmentSceneModifier();

        /// @brief Advances the attachments, when time passes and there are characters.
        virtual void modify(hkbSceneCharacters& sceneCharacters, hkReal timestep);   ///< slot 3
    };
} }
