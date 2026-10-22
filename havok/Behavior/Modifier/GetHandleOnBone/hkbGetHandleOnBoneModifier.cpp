/// @file
/// @brief hkbGetHandleOnBoneModifier, reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/Modifier/GetHandleOnBone/hkbGetHandleOnBoneModifier.h>

#include <Animation/Ragdoll/Instance/hkaRagdollInstance.h>
#include <Behavior/Driver/Ragdoll/hkbRagdollDriver.h>
#include <Behavior/Modifier/SenseHandle/hkbSenseHandleModifier.h>
#include <Behavior/Utils/hkbClosestLocalFrameCollector.h>
#include <Physics/Dynamics/Entity/hkpRigidBody.h>
#include <Physics/Dynamics/World/hkpWorld.h>

namespace CB { namespace havok
{
    /// @note Not in the inventory (only .pdata references it); the TU's first function.
    hkbGetHandleOnBoneModifier::hkbGetHandleOnBoneModifier()
    :   m_ragdollBoneIndex(-1),
        m_animationBoneIndex(-1)
    {
    }

    hkbGetHandleOnBoneModifier::hkbGetHandleOnBoneModifier(const hkbGetHandleOnBoneModifier& mod)
    :   hkbModifier(mod),
        m_localFrameName(mod.m_localFrameName),
        m_ragdollBoneIndex(mod.m_ragdollBoneIndex),
        m_animationBoneIndex(mod.m_animationBoneIndex)
    {
    }

    /// @note A ragdoll bone wins over an animation bone. With a local frame name the handle is the nearest frame of that
    ///       name on the bone (none found leaves it cleared); a ragdoll body's owning character comes from its
    ///       PROPERTY_CHARACTER property, read under the world lock. Local frames on bones are sorted by bone index.
    void hkbGetHandleOnBoneModifier::activate(const hkbContext& context)
    {
        if (!m_handleOut) return;

        m_handleOut->clear();

        hkbRagdollDriver*   driver  = context.m_character->m_ragdollDriver;
        hkaRagdollInstance* ragdoll = (driver == HK_NULL) ? HK_NULL : driver->getRagdoll();

        if (m_ragdollBoneIndex >= 0) {
            if (!ragdoll) return;

            const int     rigidBodyIndex = ragdoll->m_boneToRigidBodyMap[m_ragdollBoneIndex];
            hkpRigidBody* rigidBody      = (rigidBodyIndex < 0) ? HK_NULL : ragdoll->m_rigidBodies[rigidBodyIndex];
            const char*   frameName = m_localFrameName.cString();
            if (!frameName || hkString::strLen(frameName) <= 0) {
                m_handleOut->m_character = context.m_character;
                m_handleOut->m_rigidBody = rigidBody;
            } else if (rigidBody->getLocalFrame()) {
                hkbClosestLocalFrameCollector collector(m_localFrameName.cString());
                collector.setRigidBody(rigidBody);
                rigidBody->getLocalFrame()->getNearbyFrames(hkVector4::getZero(), HK_REAL_MAX, collector);
                if (collector.m_closestLocalFrame.m_rigidBody || collector.m_closestLocalFrame.m_character) {
                    m_handleOut->set(collector.m_closestLocalFrame);
                    context.getWorld()->lock();
                    hkbCharacter* character = reinterpret_cast<hkbCharacter*>(m_handleOut->m_rigidBody->getProperty(hkbSenseHandleModifier::PROPERTY_CHARACTER));
                    context.getWorld()->unlock();
                    m_handleOut->m_character = character;
                }
            }
        } else if (m_animationBoneIndex != -1) {
            const char* frameName = m_localFrameName.cString();
            if (!frameName || hkString::strLen(frameName) <= 0) {
                m_handleOut->m_character          = context.m_character;
                m_handleOut->m_animationBoneIndex = m_animationBoneIndex;
                return;
            }

            const hkaSkeleton* skeleton = context.m_character->m_setup->m_animationSkeleton;
            if (!skeleton) return;

            for (int i = 0; i < skeleton->m_localFrames.getSize(); i++) {
                if (skeleton->m_localFrames[i].m_boneIndex == m_animationBoneIndex) {
                    hkLocalFrame* localFrame = skeleton->m_localFrames[i].m_localFrame;
                    if (!localFrame) return;

                    hkbClosestLocalFrameCollector collector(m_localFrameName.cString());
                    collector.setCharacterAndBone(context.m_character, m_animationBoneIndex);
                    localFrame->getNearbyFrames(hkVector4::getZero(), HK_REAL_MAX, collector);
                    if (collector.m_closestLocalFrame.m_rigidBody || collector.m_closestLocalFrame.m_character) {
                        m_handleOut->set(collector.m_closestLocalFrame);
                    }
                    return;
                }
                if (skeleton->m_localFrames[i].m_boneIndex > m_animationBoneIndex) return;
            }
        } else {
            m_handleOut->m_character = context.m_character;
        }
    }

    void hkbGetHandleOnBoneModifier::deactivate(const hkbContext& context)
    {
        m_handleOut = HK_NULL;
    }

    /// @note A bone index may be unset (-1) when a variable binds it.
    bool hkbGetHandleOnBoneModifier::isValid(hkStringPtr& errorString) const
    {
        const hkbVariableBindingSet* bindings = m_variableBindingSet;
        const bool usingRagdollBone   = m_ragdollBoneIndex >= 0 || (bindings && bindings->findBindingByMemberPath("ragdollBoneIndex"));
        const bool usingAnimationBone = m_animationBoneIndex >= 0 || (bindings && bindings->findBindingByMemberPath("animationBoneIndex"));

        if (usingRagdollBone && usingAnimationBone) {
            errorString = "The animation and the ragdoll bone cannot both be set.";
            return false;
        }
        return true;
    }

    hkbNode* hkbGetHandleOnBoneModifier::cloneNode(hkbBehaviorGraph& rootBehavior) const
    {
        return new hkbGetHandleOnBoneModifier(*this);
    }
} }
