/// @file
/// @brief hkbAttachmentModifier, reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/Modifier/Attachment/hkbAttachmentModifier.h>

#include <Behavior/Attachment/hkbAttachmentInstance.h>
#include <Behavior/Attachment/hkbAttachmentManager.h>
#include <Behavior/BehaviorGraph/hkbBehaviorGraph.h>
#include <Behavior/Driver/Ragdoll/hkbRagdollDriver.h>
#include <Common/Base/Memory/Util/Stats/hkStatisticsCollector.h>
#include <Physics/Dynamics/Entity/hkpRigidBody.h>
#include <Physics/Dynamics/World/hkpWorld.h>

namespace CB { namespace havok
{
    hkbAttachmentModifier::hkbAttachmentModifier()
    :   m_attacherHandle(HK_NULL),
        m_attacheeHandle(HK_NULL),
        m_attacheeLayer(-1),
        m_attacheeRB(HK_NULL),
        m_oldMotionType(0),
        m_oldFilterInfo(0),
        m_attachment(HK_NULL)
    {
    }

    hkbAttachmentModifier::hkbAttachmentModifier(const hkbAttachmentModifier& mod)
    :   hkbModifier(mod),
        m_attacherHandle(HK_NULL),
        m_attacheeHandle(HK_NULL),
        m_attacheeRB(HK_NULL),
        m_oldMotionType(0),
        m_oldFilterInfo(0),
        m_attachment(HK_NULL)
    {
        hkString::memCpy(&m_sendToAttacherOnAttach, &mod.m_sendToAttacherOnAttach,
                         int(reinterpret_cast<const char*>(&mod.m_attacheeLayer + 1) - reinterpret_cast<const char*>(&mod.m_sendToAttacherOnAttach)));
        if (mod.m_attachmentSetup) m_attachmentSetup.setAndDontIncrementRefCount(new hkbAttachmentSetup(*mod.m_attachmentSetup));
    }

    void hkbAttachmentModifier::calcContentStatistics(hkStatisticsCollector* collector, const hkClass* cls) const
    {
        hkStatisticsCollectorUtil::addDeclaredMembers(this, &hkbAttachmentModifierClass, collector);
        if (m_attachment) {
            collector->addNormalChunk("hkbAttachmentInstance", m_attachment, sizeof(hkbAttachmentInstance));
            collector->addReferencedObject("hkpConstraintInstance", m_attachment->m_constraint);
        }
        hkbModifier::calcContentStatistics(collector, &hkbModifierClass);
    }

    void hkbAttachmentModifier::activate(const hkbContext& context)
    {
        m_attacheeRB    = HK_NULL;
        m_oldFilterInfo = 0;
        m_attachment    = HK_NULL;
    }

    void hkbAttachmentModifier::update(const hkbContext& context, hkReal timestep)
    {
        if (m_attachment == HK_NULL) attach(context);
    }

    void hkbAttachmentModifier::deactivate(const hkbContext& context)
    {
        detach(context);
        if (m_attacherHandle) {
            m_attacherHandle->removeReference();
            m_attacherHandle = HK_NULL;
        }
        if (m_attacheeHandle) {
            m_attacheeHandle->removeReference();
            m_attacheeHandle = HK_NULL;
        }
    }

    void hkbAttachmentModifier::setAttachmentSetup(hkbAttachmentSetup* attachmentSetup)
    {
        m_attachmentSetup = attachmentSetup;
    }

    hkbNode* hkbAttachmentModifier::cloneNode(hkbBehaviorGraph& rootBehavior) const
    {
        return new hkbAttachmentModifier(*this);
    }

    void hkbAttachmentModifier::attach(const hkbContext& context)
    {
        hkbHandle* attacherHandle = m_attacherHandle;
        if (attacherHandle == HK_NULL) return;
        hkbHandle* attacheeHandle = m_attacheeHandle;
        if (attacheeHandle == HK_NULL) return;
        if (attacherHandle->m_rigidBody == HK_NULL && attacherHandle->m_character == HK_NULL) return;
        if (attacheeHandle->m_rigidBody == HK_NULL && attacheeHandle->m_character == HK_NULL) return;

        const hkInt8 attachmentType = m_attachmentSetup->m_attachmentType;
        if (attachmentType == hkbAttachmentSetup::ATTACHMENT_TYPE_BALL_SOCKET_CONSTRAINT ||
            attachmentType == hkbAttachmentSetup::ATTACHMENT_TYPE_RAGDOLL_CONSTRAINT) {
            if (attacherHandle->m_rigidBody == HK_NULL || attacheeHandle->m_rigidBody == HK_NULL) return;
        }
        if (attachmentType == hkbAttachmentSetup::ATTACHMENT_TYPE_SET_WORLD_FROM_MODEL) {
            if (attacherHandle->m_character == HK_NULL || attacheeHandle->m_character == HK_NULL) return;
        }

        m_attacheeRB = attacheeHandle->m_rigidBody;
        if (m_attacheeRB) {
            if (m_attacheeRB->getWorld() == HK_NULL) {
                m_attacheeRB = HK_NULL;
                return;
            }
            if (attacherHandle->m_rigidBody && attacherHandle->m_rigidBody->getWorld() == HK_NULL) return;

            m_attacheeRB->getWorld()->lock();
            if (m_attachmentSetup->m_attachmentType == hkbAttachmentSetup::ATTACHMENT_TYPE_KEYFRAME_RIGID_BODY) {
                hkpRigidBody* rb = m_attacheeRB;
                if (rb->getMotionType() != hkpMotion::MOTION_KEYFRAMED || rb->getQualityType() != HK_COLLIDABLE_QUALITY_KEYFRAMED) {
                    m_oldMotionType = static_cast<hkUint8>(rb->getMotionType());
                    rb->setMotionType(hkpMotion::MOTION_KEYFRAMED, HK_ENTITY_ACTIVATION_DO_ACTIVATE);
                    rb->getCollidableRw()->setQualityType(HK_COLLIDABLE_QUALITY_KEYFRAMED);
                }
            }

            hkpRigidBody* rb                 = m_attacheeRB;
            hkbCharacter* attacheeCharacter = m_attacheeHandle->m_character;
            if (attacheeCharacter && attacheeCharacter->m_ragdollDriver) attacheeCharacter->m_ragdollDriver->addAttachedRigidBody(rb);

            const hkUint32 filterInfo = rb->getCollisionFilterInfo();
            m_oldFilterInfo           = filterInfo;
            if (m_attacheeLayer == -1) {
                const hkpRigidBody* attacherRB = m_attacherHandle->m_rigidBody;
                if (attacherRB) {
                    const hkUint32 attacherInfo = attacherRB->getCollisionFilterInfo();
                    const int      subSystemId  = hkpGroupFilter::getSubSystemIdFromFilterInfo(attacherInfo);
                    rb->getCollidableRw()->setCollisionFilterInfo(hkpGroupFilter::calcFilterInfo(hkpGroupFilter::getLayerFromFilterInfo(filterInfo),
                                                                                                 hkpGroupFilter::getSystemGroupFromFilterInfo(attacherInfo),
                                                                                                 subSystemId, subSystemId));
                }
            } else {
                rb->getCollidableRw()->setCollisionFilterInfo(hkpGroupFilter::calcFilterInfo(m_attacheeLayer, hkpGroupFilter::getSystemGroupFromFilterInfo(filterInfo),
                                                                           hkpGroupFilter::getSubSystemIdFromFilterInfo(filterInfo),
                                                                           hkpGroupFilter::getSubSystemDontCollideWithFromFilterInfo(filterInfo)));
            }
            context.getWorld()->updateCollisionFilterOnEntity(rb, 0, 1);
            m_attacheeRB->getWorld()->unlock();
        }

        hkbAttachmentSetup* setup = m_attachmentSetup;
        hkbAttachmentManager::AttachmentHandles handles;
        handles.m_attacherHandle.set(*m_attacherHandle);
        handles.m_attacheeHandle.set(*m_attacheeHandle);
        hkbAttachmentInstance* attachment = context.getAttachmentManager()->attachHandles(context.getWorld(), setup, handles);
        m_attachment                      = attachment;

        const hkbSymbolIdMap* eventIdMap = context.m_behavior->m_eventIdMap;
        if (m_sendToAttacheeOnAttach.m_id != -1 && attachment->m_attacheeHandle.m_character) {
            hkbEvent e(m_sendToAttacheeOnAttach.m_id, m_sendToAttacheeOnAttach.getPayload(), HK_NULL);
            context.sendEventToCharacter(e, attachment->m_attacheeHandle.m_character, eventIdMap);
        }
        if (m_sendToAttacherOnAttach.m_id != -1 && attachment->m_attacherHandle.m_character) {
            hkbEvent e(m_sendToAttacherOnAttach.m_id, m_sendToAttacherOnAttach.getPayload(), HK_NULL);
            context.sendEventToCharacter(e, attachment->m_attacherHandle.m_character, eventIdMap);
        }
    }

    void hkbAttachmentModifier::detach(const hkbContext& context)
    {
        if (m_attachment == HK_NULL) return;

        hkpRigidBody* rb = m_attacheeRB;
        if (rb) {
            hkbCharacter* attacheeCharacter = m_attachment->m_attacheeHandle.m_character;
            if (attacheeCharacter && attacheeCharacter->m_ragdollDriver) attacheeCharacter->m_ragdollDriver->removeAttachedRigidBody(rb);
            if (rb->getWorld()) {
                rb->getWorld()->lock();
                if (m_attachmentSetup->m_attachmentType == hkbAttachmentSetup::ATTACHMENT_TYPE_KEYFRAME_RIGID_BODY && m_oldMotionType != 0 &&
                    (rb->getMotionType() == hkpMotion::MOTION_KEYFRAMED || rb->getQualityType() != HK_COLLIDABLE_QUALITY_MOVING)) {
                    rb->setMotionType(m_oldMotionType, HK_ENTITY_ACTIVATION_DO_ACTIVATE);
                    rb->getCollidableRw()->setQualityType(HK_COLLIDABLE_QUALITY_MOVING);
                }
                hkpWorld* world           = rb->getWorld();
                rb->getCollidableRw()->setCollisionFilterInfo(m_oldFilterInfo);
                if (world == context.getWorld()) context.getWorld()->updateCollisionFilterOnEntity(rb, 0, 1);
                rb->getWorld()->unlock();
            }
            m_attacheeRB = HK_NULL;
        }

        hkbAttachmentInstance* attachment = m_attachment;
        const hkbSymbolIdMap*  eventIdMap = context.m_behavior->m_eventIdMap;
        hkbCharacter*          attachee   = attachment->m_attacheeHandle.m_character;
        if (m_sendToAttacheeOnDetach.m_id != -1 && attachee) {
            hkbEvent e(m_sendToAttacheeOnDetach.m_id, m_sendToAttacheeOnDetach.getPayload(), HK_NULL);
            context.sendEventToCharacter(e, attachee, eventIdMap);
        }
        hkbCharacter* attacher = attachment->m_attacherHandle.m_character;
        if (m_sendToAttacherOnDetach.m_id != -1 && attacher) {
            hkbEvent e(m_sendToAttacherOnDetach.m_id, m_sendToAttacherOnDetach.getPayload(), HK_NULL);
            context.sendEventToCharacter(e, attacher, eventIdMap);
        }
        context.getAttachmentManager()->detach(context.getWorld(), m_attachment);
        m_attachment = HK_NULL;
    }
} }
