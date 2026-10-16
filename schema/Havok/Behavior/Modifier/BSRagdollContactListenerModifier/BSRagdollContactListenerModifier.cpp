/// @file
/// @brief BSRagdollContactListenerModifier, reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/Modifier/BSRagdollContactListenerModifier/BSRagdollContactListenerModifier.h>

#include <Animation/Ragdoll/Instance/hkaRagdollInstance.h>
#include <Behavior/Driver/Ragdoll/hkbRagdollDriver.h>
#include <Common/Base/Container/LocalArray/hkLocalArray.h>
#include <Common/Base/Container/PointerMap/hkPointerMap.h>
#include <Physics/Dynamics/Entity/hkpRigidBody.h>
#include <Physics/Dynamics/World/hkpWorld.h>

namespace CB { namespace schema
{
    void BSRagdollContactListenerModifier::setListenersActive(const hkbContext& context, BSRagdollContactListenerModifier* modifier, bool activate)
    {
        if (BSIRagdollContactListenerModifierSingleton::s_instance) {
            BSIRagdollContactListenerModifierSingleton::s_instance->SetListenersActive(context, modifier, activate);
        }
        else if (activate) {
            modifier->addListeners(context);
        }
        else {
            modifier->removeListeners();
        }
    }

    BSIRagdollContactListenerModifierSingleton::BSIRagdollContactListenerModifierSingleton()
    {
    }

    BSIRagdollContactListenerModifierSingleton::~BSIRagdollContactListenerModifierSingleton()
    {
    }

    BSRagdollContactListenerModifier::BSRagdollContactListenerModifier()
    :   m_throwEvent(false)
    {
        m_userData |= 2;
    }

    BSRagdollContactListenerModifier::BSRagdollContactListenerModifier(hkFinishLoadedObjectFlag flag)
    :   hkbModifier(flag),
        m_contactEvent(flag),
        m_bones(flag),
        m_throwEvent(false)
    {
        m_userData |= 2;
    }

    BSRagdollContactListenerModifier::~BSRagdollContactListenerModifier()
    {
    }

    const void* BSRagdollContactListenerModifier::getClassType() const
    {
        return &staticClass();
    }

    void BSRagdollContactListenerModifier::collectBindables(hkbBindableCollector& collector)
    {
        collector.collectBindable(this);
        if (m_bones) m_bones->collectBindables(collector);
    }

    hkbNode* BSRagdollContactListenerModifier::cloneNode(hkbBehaviorGraph& rootBehavior) const
    {
        return new BSRagdollContactListenerModifier(*this);
    }

    void BSRagdollContactListenerModifier::activate(const hkbContext& context)
    {
        setListenersActive(context, this, true);
    }

    void BSRagdollContactListenerModifier::deactivate(const hkbContext& context)
    {
        setListenersActive(context, this, false);
    }

    /// @note The bone indices are copied into a local array first; the seen-set is a map over stack storage.
    bool BSRagdollContactListenerModifier::isValid(hkStringPtr& errorString) const
    {
        bool valid = true;
        if (m_contactEvent.m_id == -1) {
            errorString = "No Event specified for contact.";
            return false;
        }
        if (m_bones && !m_bones->m_variableBindingSet) {
            const int numBones = m_bones->m_boneIndices.getSize();
            if (numBones > 0) {
                hkLocalArray<hkInt16> boneIndices(numBones);
                if (m_bones && !m_bones->m_variableBindingSet) {
                    for (int i = 0; i < m_bones->m_boneIndices.getSize(); i++) boneIndices.pushBack(m_bones->m_boneIndices[i]);
                }

                const int mapSize = hkPointerMap<hkInt16, int>::getSizeInBytesFor(numBones);
                char*     storage = hkAllocateStack<char>(mapSize);
                hkLocalPointerMap<hkInt16, int> usedBones(storage, mapSize);
                for (int i = 0; i < boneIndices.getSize(); i++) {
                    if (!valid) break;
                    if (boneIndices[i] == -1) {
                        errorString = "Unspecified bone index.";
                        valid       = false;
                    }
                    if (usedBones.getWithDefault(boneIndices[i], -1) != -1) {
                        errorString = "Duplicate bone index.";
                        valid       = false;
                    }
                    else if (valid) {
                        usedBones.insert(boneIndices[i], 1);
                    }
                }
            }
        }
        return valid;
    }

    void BSRagdollContactListenerModifier::update(const hkbContext& context, hkReal timestep)
    {
        if (m_throwEvent && m_contactEvent.m_id != -1) {
            hkbEventQueue* queue = context.getEventQueue();
            hkbEvent       event(m_contactEvent.m_id, m_contactEvent.m_payload, this);
            event.m_id = queue->toExternal(event.m_id);
            queue->enqueue(event);
        }
        m_throwEvent = false;
    }

    void BSRagdollContactListenerModifier::contactPointCallback(const hkpContactPointEvent& event)
    {
        if (event.m_contactPoint->getDistance() < 0.0f) {
            if (m_rigidBodies.lastIndexOf(event.m_bodies[0]) == -1 || m_rigidBodies.lastIndexOf(event.m_bodies[1]) == -1) m_throwEvent = true;
        }
    }

    /// @note A body counts only while heap-allocated (getAllocatedSize, as vanilla tests it).
    void BSRagdollContactListenerModifier::addListeners(const hkbContext& context)
    {
        removeListeners();

        if (m_bones && context.m_character && context.m_character->m_ragdollDriver && context.m_character->m_ragdollDriver->getRagdoll()
            && context.getWorld()) {
            m_rigidBodies.reserveExactly(m_bones->m_boneIndices.getSize());
            for (int i = 0; i < m_bones->m_boneIndices.getSize(); i++) {
                hkbRagdollDriver*   driver  = context.m_character->m_ragdollDriver;
                hkaRagdollInstance* ragdoll = (driver == HK_NULL) ? HK_NULL : driver->getRagdoll();
                hkpRigidBody*       body    = ragdoll->getRigidBodyOfBone(m_bones->m_boneIndices[i]);
                if (body && body->getAllocatedSize() != 0) {
                    if (body->getMotionType() == hkpMotion::MOTION_KEYFRAMED) {
                        body->getCollidableRw()->setQualityType(HK_COLLIDABLE_QUALITY_KEYFRAMED_REPORTING);
                        if (body->getWorld()) {
                            body->getWorld()->updateCollisionFilterOnEntity(body, HK_UPDATE_FILTER_ON_ENTITY_FULL_CHECK,
                                                                            HK_UPDATE_COLLECTION_FILTER_PROCESS_SHAPE_COLLECTIONS);
                        }
                    }
                    body->addContactListener(this);
                    body->addReference();
                    m_rigidBodies.pushBack(body);
                }
            }
        }
    }

    /// @note The release goes through the array slot again (vanilla re-reads it after removeContactListener).
    void BSRagdollContactListenerModifier::removeListeners()
    {
        for (int i = 0; i < m_rigidBodies.getSize(); i++) {
            hkpRigidBody* body = m_rigidBodies[i];
            if (body) {
                hkpContactListener* listener = this;
                if (body->getContactListeners().indexOf(listener) != -1) body->removeContactListener(listener);
                m_rigidBodies[i]->removeReference();
            }
        }
        m_rigidBodies.clear();
    }

    BSRagdollContactListenerModifier::BSRagdollContactListenerModifier(const BSRagdollContactListenerModifier& mod)
    :   hkbModifier(mod),
        m_contactEvent(mod.m_contactEvent),
        m_throwEvent(mod.m_throwEvent)
    {
        m_userData |= 2;
        if (mod.m_bones) {
            const hkbVariableBindingSet* bindings = mod.m_bones->m_variableBindingSet;
            if (bindings && bindings->m_bindings.getSize() > 0) {
                m_bones.setAndDontIncrementRefCount(new hkbBoneIndexArray(*mod.m_bones));
            }
            else {
                m_bones = mod.m_bones;
            }
        }
    }
} }
