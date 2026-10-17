/// @file
/// @brief hkbAttachmentFixupSceneModifier: the second attachment pass, which fixes the attachments up after the
///        characters generate.
#pragma once

#include <Behavior/Attachment/hkbAttachmentManager.h>
#include <Behavior/World/hkbSceneModifier.h>

namespace CB { namespace schema
{
    /// @note The destructor is implicit (vanilla's scalar deleting destructor stamps only hkBaseObject's vtable).
    class hkbAttachmentFixupSceneModifier : public hkbSceneModifier
    {
    public:
        /// @note Not owned: hkbWorld sets it to its own manager after construction.
        hkbAttachmentManager* m_attachmentManager;   ///< +0x10

        hkbAttachmentFixupSceneModifier();

        /// @brief Fixes the attachments up (hkbAttachmentManager::update's fixup pass), when time passes.
        virtual void modify(hkbSceneCharacters& sceneCharacters, hkReal timestep);   ///< slot 3
    };
} }
