/// @file
/// @brief hkbCharacter, reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/Character/hkbCharacter.h>

#include <Behavior/BehaviorGraph/hkbBehaviorGraph.h>
#include <Behavior/Context/hkbContext.h>
#include <Behavior/Driver/CharacterController/hkbCharacterControllerDriver.h>
#include <Behavior/Driver/FootIk/hkbFootIkDriver.h>
#include <Behavior/Driver/HandIk/hkbHandIkDriver.h>
#include <Behavior/Driver/Ragdoll/hkbRagdollDriver.h>
#include <Behavior/World/hkbWorld.h>

namespace CB { namespace schema
{
    void hkbCharacter::getNearbyCharacters(hkReal radius, hkArray<hkbCharacter*>& charactersOut)
    {
        const int        numNearby = m_nearbyCharacters.getSize();
        const hkVector4& here      = m_worldFromModel->m_translation;
        for (int i = 0; i < numNearby; i++) {
            hkVector4 diff;
            diff.setSub4(m_nearbyCharacters[i]->m_worldFromModel->m_translation, here);
            if (diff.length3().getReal() <= radius) charactersOut.pushBack(m_nearbyCharacters[i]);
        }
    }

    void hkbCharacter::activate()
    {
        if (m_behaviorGraph && !m_behaviorGraph->m_isActive) {
            initPoseLocal();
            hkbContext context(this, (m_world == HK_NULL) ? HK_NULL : m_world->m_physicsWorld, (m_world == HK_NULL) ? HK_NULL : m_world->m_attachmentManager, HK_NULL);
            m_behaviorGraph->activate(context);
        }
    }

    void hkbCharacter::activateWithInternalState(const hkbBehaviorGraphInternalState& internalState)
    {
        if (m_behaviorGraph && !m_behaviorGraph->m_isActive) {
            initPoseLocal();
            hkbContext context(this, (m_world == HK_NULL) ? HK_NULL : m_world->m_physicsWorld, (m_world == HK_NULL) ? HK_NULL : m_world->m_attachmentManager, HK_NULL);
            m_behaviorGraph->activateWithInternalState(context, internalState);
        }
    }

    void hkbCharacter::deactivate()
    {
        if (m_behaviorGraph && m_behaviorGraph->m_isActive) {
            hkbContext context(this, (m_world == HK_NULL) ? HK_NULL : m_world->m_physicsWorld, (m_world == HK_NULL) ? HK_NULL : m_world->m_attachmentManager, HK_NULL);
            m_behaviorGraph->deactivate(context);
            if (m_footIkDriver) m_footIkDriver->reset();
            if (m_handIkDriver) m_handIkDriver->reset();
            if (m_characterControllerDriver) m_characterControllerDriver->reset();
            if (m_ragdollDriver) m_ragdollDriver->reset();
            deletePoseLocal();
        }
    }

    void hkbCharacter::handleEvent(const hkbEvent& e)
    {
        if (m_behaviorGraph && m_behaviorGraph->m_isActive) {
            hkbContext context(this, (m_world == HK_NULL) ? HK_NULL : m_world->m_physicsWorld, (m_world == HK_NULL) ? HK_NULL : m_world->m_attachmentManager, HK_NULL);
            m_behaviorGraph->handleEvent(context, e);
        }
    }

    hkStringPtr hkbCharacter::getName() const
    {
        if (m_name.cString()) return m_name;
        if (m_setup && m_setup->m_data && m_setup->m_data->m_stringData) return m_setup->m_data->m_stringData->m_name;
        return hkStringPtr();
    }

    const hkQsTransform* hkbCharacter::getPoseLocal()
    {
        if (m_poseLocal == HK_NULL) initPoseLocal();
        return m_poseLocal;
    }

    hkbAnimationBindingSet* hkbCharacter::accessAnimationBindingSet()
    {
        if (m_animationBindingSet) return m_animationBindingSet;
        if (m_setup->m_animationBindingSet == HK_NULL) {
            m_setup->m_animationBindingSet.setAndDontIncrementRefCount(new hkbAnimationBindingSet());
        }
        return m_setup->m_animationBindingSet;
    }

    void hkbCharacter::setAnimationBindingSet(hkbAnimationBindingSet* animationBindingSet)
    {
        m_animationBindingSet = animationBindingSet;
    }

    void hkbCharacter::setWorldFromModel(hkQsTransform* worldFromModel, bool deleteWorldFromModel)
    {
        if (m_deleteWorldFromModel && m_worldFromModel) hkDeallocateChunk(m_worldFromModel, 1);
        m_worldFromModel       = worldFromModel;
        m_deleteWorldFromModel = deleteWorldFromModel;
    }

    void hkbCharacter::setPoseLocal(hkQsTransform* poseLocal, bool deletePoseLocal)
    {
        if (m_deletePoseLocal) hkDeallocate(m_poseLocal);
        m_poseLocal       = poseLocal;
        m_deletePoseLocal = deletePoseLocal;
    }

    hkbRaycastInterface* hkbCharacter::getRaycastInterface() const
    {
        if (m_raycastInterface == HK_NULL && m_world) return m_world->m_raycastInterface;
        return m_raycastInterface;
    }

    /// @note Each listener lookup calls getListeners() again, as vanilla does.
    void hkbCharacter::setBehavior(hkbBehaviorGraph* behavior)
    {
        hkbBehaviorGraph* oldBehavior = m_behaviorGraph;
        if (behavior) behavior->addReference();
        if (m_behaviorGraph) m_behaviorGraph->removeReference();
        m_behaviorGraph = behavior;
        if (m_world) {
            for (int i = 0; i < m_world->getListeners().getSize(); i++) {
                m_world->getListeners()[i]->characterBehaviorChangedCallback(this, oldBehavior);
            }
        }
    }

    void hkbCharacter::setPoseLocalBuffer(hkQsTransform* poseLocal, int numPoseLocal)
    {
        m_poseLocal    = poseLocal;
        m_numPoseLocal = numPoseLocal;
    }

    void hkbCharacter::initPoseLocal()
    {
        if (m_poseLocal == HK_NULL) {
            const int numBones = m_setup->m_animationSkeleton->m_bones.getSize();
            m_numPoseLocal     = numBones;
            m_poseLocal        = hkAllocate<hkQsTransform>(numBones);
            const hkaSkeleton* skeleton = m_setup->m_animationSkeleton;
            hkString::memCpy(m_poseLocal, skeleton->m_referencePose.m_data, skeleton->m_referencePose.getSize() * int(sizeof(hkQsTransform)));
            m_deletePoseLocal = true;
        }
    }

    void hkbCharacter::deletePoseLocal()
    {
        if (m_deletePoseLocal) {
            hkDeallocate(m_poseLocal);
            m_poseLocal       = HK_NULL;
            m_deletePoseLocal = false;
        }
    }

    hkbCharacter::hkbCharacter(hkFinishLoadedObjectFlag flag)
    :   hkReferencedObject(flag),
        m_nearbyCharacters(flag),
        m_name(flag),
        m_ragdollDriver(flag),
        m_characterControllerDriver(flag),
        m_footIkDriver(flag),
        m_setup(flag)
    {
    }
} }
