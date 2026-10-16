/// @file
/// @brief hkbAttachmentManager: the behavior world's live attachments (declarations only so far).
#pragma once

#include <Behavior/Utils/hkbHandle.h>

namespace CB { namespace schema
{
    struct hkbAttachmentInstance;
    class hkbAttachmentSetup;
    class hkpWorld;
    struct hkbSceneCharacters;

    /// @note 0x10 bytes, heap-allocated by hkbWorld's constructor (hkbWorld +0x130), not reference counted.
    struct hkbAttachmentManager
    {
        /// @brief The two ends attachHandles is handed, adjacent as in vanilla (one pointer passed).
        /// @note Name provisional. Its handles start cleared (vanilla zeroes them before copying the targets in); it has
        ///       no array of handles: a local array's construct/destruct loops differ from vanilla's straight-line code.
        struct AttachmentHandles
        {
            hkbHandle m_attacherHandle;
            hkbHandle m_attacheeHandle;

            HK_FORCE_INLINE AttachmentHandles()
            {
                m_attacherHandle.clear();
                m_attacheeHandle.clear();
            }
        };

        hkArray<hkbAttachmentInstance*> m_attachments;   ///< +0x00

        /// @brief Advances every attachment by `timestep`: blends the rigid-body ones in, and moves the
        ///        set-world-from-model ones into registration (`isFixup`: the second pass, after the characters
        ///        generate, which does not advance the blend).
        /// @note Name and parameter names provisional; out of line in vanilla (0x140ae1f60).
        void update(hkReal timestep, hkbSceneCharacters& sceneCharacters, bool isFixup);
        /// @brief Forms an attachment of the attachee handle to the attacher handle as `setup` says.
        /// @note Name and parameter names provisional; out of line in vanilla (0x140ae1d80).
        hkbAttachmentInstance* attachHandles(hkpWorld* world, hkbAttachmentSetup* setup, const AttachmentHandles& handles);
        /// @brief Releases `attachment` and frees it.
        /// @note Name and parameter names provisional; out of line in vanilla (0x140ae1ed0).
        void detach(hkpWorld* world, hkbAttachmentInstance* attachment);
    };
} }
