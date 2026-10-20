/// @file
/// @brief hkbAttachmentManager, reimplemented against vanilla (AE 1.6.1170): the functions matched so far.
#include <Behavior/Attachment/hkbAttachmentManager.h>

#include <Behavior/Attachment/hkbAttachmentInstance.h>
#include <Physics/Dynamics/World/hkpWorld.h>

namespace CB { namespace havok
{
    hkbAttachmentManager::~hkbAttachmentManager()
    {
        for (int i = m_attachments.getSize() - 1; i >= 0; i--) {
            detach(HK_NULL, m_attachments[i]);
        }
    }

    /// @note Destroys and frees the instance without a null check (vanilla calls no `delete`, which would test it).
    void hkbAttachmentManager::detach(hkpWorld* world, hkbAttachmentInstance* attachment)
    {
        removeConstraint(world, attachment);
        removeAttachment(attachment);
        attachment->~hkbAttachmentInstance();
        hkMemoryRouter::getInstance().heap().blockFree(attachment, int(sizeof(hkbAttachmentInstance)));
    }

    void hkbAttachmentManager::removeConstraint(hkpWorld* world, hkbAttachmentInstance* attachment)
    {
        if (attachment->m_constraint) {
            if (world && attachment->m_constraint->getOwner()) {
                world->lock();
                world->removeConstraint(attachment->m_constraint);
                world->unlock();
            }
            attachment->m_constraint->removeReference();
            attachment->m_constraint = HK_NULL;
        }
    }

    void hkbAttachmentManager::removeAttachment(hkbAttachmentInstance* attachment)
    {
        for (int i = 0; i < m_attachments.getSize(); i++) {
            if (m_attachments[i] == attachment) {
                m_attachments.removeAtAndCopy(i);
                return;
            }
        }
    }
} }
