/// @file
/// @brief hkbAttachmentInstance: one live attachment the attachment manager keeps.
#pragma once

#include <Behavior/Attachment/hkbAttachmentSetup.h>
#include <Behavior/Utils/hkbHandle.h>
#include <Physics/Dynamics/Constraint/hkpConstraintInstance.h>
#include <Physics/Dynamics/Entity/hkpRigidBody.h>

namespace CB { namespace havok
{
    /// @note 0x78 bytes, not reference counted (hkbAttachmentManager::detach frees it with its size). Only what the
    ///       reimplemented code reads is declared; names provisional.
    struct hkbAttachmentInstance
    {
        hkbAttachmentSetup*    m_setup;            ///< +0x00 — referenced
        /// @note The handles' rigid bodies are referenced too (the destructor releases them).
        hkbHandle              m_attacherHandle;   ///< +0x08
        hkbHandle              m_attacheeHandle;   ///< +0x38
        hkUint8                m_pad68[0x70 - 0x68];
        /// @note Referenced; the constraint attachment types only.
        hkpConstraintInstance* m_constraint;       ///< +0x70

        /// @pre The setup is set (it is released unchecked, as in vanilla).
        HK_FORCE_INLINE ~hkbAttachmentInstance()
        {
            m_setup->removeReference();
            if (m_attacherHandle.m_rigidBody) m_attacherHandle.m_rigidBody->removeReference();
            if (m_attacheeHandle.m_rigidBody) m_attacheeHandle.m_rigidBody->removeReference();
        }
    };
} }
