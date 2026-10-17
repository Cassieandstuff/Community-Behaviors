/// @file
/// @brief hkbDetectCloseToGroundModifier, reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/Modifier/DetectCloseToGround/hkbDetectCloseToGroundModifier.h>

#include <Animation/Ragdoll/Instance/hkaRagdollInstance.h>
#include <Behavior/BehaviorGraph/hkbBehaviorGraph.h>
#include <Behavior/Driver/Ragdoll/hkbRagdollDriver.h>
#include <Behavior/Generator/hkbGeneratorOutputUtils.h>
#include <Physics/Dynamics/Entity/hkpRigidBody.h>
#include <Physics/Dynamics/World/hkpWorld.h>

namespace CB { namespace schema
{
    hkbDetectCloseToGroundModifier::hkbDetectCloseToGroundModifier()
    :   m_closeToGroundHeight(0.5f),
        m_raycastDistanceDown(0.8f),
        m_collisionFilterInfo(3),
        m_boneIndex(-1),
        m_animBoneIndex(-1),
        m_isCloseToGround(false)
    {
    }

    hkbDetectCloseToGroundModifier::hkbDetectCloseToGroundModifier(const hkbDetectCloseToGroundModifier& mod)
    :   hkbModifier(mod),
        m_isCloseToGround(false)
    {
        hkString::memCpy(&m_closeToGroundEvent, &mod.m_closeToGroundEvent, 0x20);
    }

    void hkbDetectCloseToGroundModifier::activate(const hkbContext& context)
    {
        m_isCloseToGround = false;
    }

    void hkbDetectCloseToGroundModifier::update(const hkbContext& context, hkReal timestep)
    {
        if (m_isCloseToGround && m_closeToGroundEvent.m_id != -1) {
            hkbCharacter*     character = context.m_character;
            hkbBehaviorGraph* behavior  = character->m_behaviorGraph;
            hkbEventQueue*    queue     = context.m_eventQueue ? context.m_eventQueue : character->m_eventQueue;
            hkbNode*          sender    = behavior->getNodeTemplate(this);
            hkbEvent          event(m_closeToGroundEvent.m_id, m_closeToGroundEvent.m_payload, sender);
            event.m_id = queue->toExternal(event.m_id);
            queue->enqueue(event);
        }
    }

    bool hkbDetectCloseToGroundModifier::isValid(hkStringPtr& errorString) const
    {
        const hkbVariableBindingSet* bindings = m_variableBindingSet;
        const bool ragdollBoneValid = m_boneIndex >= 0 || (bindings && bindings->findBindingByMemberPath("boneIndex"));
        const bool animBoneValid    = m_animBoneIndex >= 0 || (bindings && bindings->findBindingByMemberPath("animBoneIndex"));

        if (!ragdollBoneValid) {
            if (animBoneValid) return true;
            errorString = "You must specify a valid bone index for detecting the ground.";
            return false;
        }
        if (animBoneValid) {
            errorString = "You cannot specify both a ragdoll bone and an animation bone for detecting the ground.";
            return false;
        }
        return true;
    }

    hkbNode* hkbDetectCloseToGroundModifier::cloneNode(hkbBehaviorGraph& rootBehavior) const
    {
        return new hkbDetectCloseToGroundModifier(*this);
    }

    hkReferencedObject* hkbDetectCloseToGroundModifier::createInternalState()
    {
        return new hkbDetectCloseToGroundModifierInternalState;
    }

    void hkbDetectCloseToGroundModifier::getInternalState(hkReferencedObject& internalState) const
    {
        static_cast<hkbDetectCloseToGroundModifierInternalState&>(internalState).m_isCloseToGround = m_isCloseToGround;
    }

    void hkbDetectCloseToGroundModifier::setInternalState(const hkReferencedObject& internalState)
    {
        m_isCloseToGround = static_cast<const hkbDetectCloseToGroundModifierInternalState&>(internalState).m_isCloseToGround;
    }

    /// @note The ragdoll path assumes the bone has a rigid body (a -1 map entry reads through null), as vanilla does.
    void hkbDetectCloseToGroundModifier::modify(const hkbContext& context, hkbGeneratorOutput& inOut)
    {
        hkaRaycastInterface* raycastInterface = context.m_character->getRaycastInterface();
        if (m_animBoneIndex < inOut.getNumPoseLocal()) {
            hkReal    hitFraction = HK_REAL_MAX;
            hkVector4 from;
            if (m_boneIndex != -1) {
                hkbRagdollDriver*   driver  = context.m_character->m_ragdollDriver;
                hkaRagdollInstance* ragdoll = (driver == HK_NULL) ? HK_NULL : driver->getRagdoll();
                const int           body    = ragdoll->m_boneToRigidBodyMap[m_boneIndex];
                hkpRigidBody*       rb      = (body >= 0) ? ragdoll->m_rigidBodies[body] : HK_NULL;
                from = rb->getPosition();
            } else {
                hkQsTransform boneModelSpace;
                hkbGeneratorOutputUtils::getBoneModelSpace(m_animBoneIndex, inOut, context.m_character->m_setup->m_animationSkeleton,
                                                           boneModelSpace);
                from.setTransformedPos(inOut.accessWorldFromModel(), boneModelSpace.m_translation);
            }

            hkVector4 to = from;
            to.addMul4(hkSimdReal(-m_raycastDistanceDown), context.getWorldUpWS());

            context.getWorld()->lock();
            hkVector4  normal;
            const bool hit = raycastInterface->castRay(from, to, m_collisionFilterInfo, hitFraction, normal);
            context.getWorld()->unlock();

            m_isCloseToGround = false;
            if (hit && hitFraction * m_raycastDistanceDown < m_closeToGroundHeight) m_isCloseToGround = true;
        }
    }
} }
