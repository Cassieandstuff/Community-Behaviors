/// @file
/// @brief hkbSenseHandleModifier, reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/Modifier/SenseHandle/hkbSenseHandleModifier.h>

#include <Animation/Ragdoll/Instance/hkaRagdollInstance.h>
#include <Behavior/BehaviorGraph/hkbBehaviorGraph.h>
#include <Behavior/Driver/Ragdoll/hkbRagdollDriver.h>
#include <Behavior/Generator/hkbGeneratorOutput.h>
#include <Behavior/Utils/hkbClosestLocalFrameCollector.h>
#include <Physics/Dynamics/Entity/hkpRigidBody.h>
#include <Physics/Dynamics/World/hkpWorld.h>

namespace CB { namespace schema
{
    /// @note Not in the inventory (only .pdata references it); the TU's first function.
    hkbSenseHandleModifier::hkbSenseHandleModifier()
    :   m_sensorLocalOffset(_mm_setzero_ps())
    {
        m_sensorRagdollBoneIndex             = -1;
        m_sensorAnimationBoneIndex           = -1;
        m_minDistance                        = 0.0f;
        m_maxDistance                        = 1.0f;
        m_distanceOut                        = 0.0f;
        m_collisionFilterInfo                = 0;
        m_sensingMode                        = SENSE_IN_NEARBY_RIGID_BODIES;
        m_extrapolateSensorPosition          = false;
        m_keepFirstSensedHandle              = false;
        m_foundHandleOut                     = false;
        m_timeSinceLastModify                = 0.0f;
        m_rangeIndexForEventToSendNextUpdate = -1;
        m_handle.clear();
    }

    /// @note The 0x17-byte memCpy copies m_minDistance through m_keepFirstSensedHandle.
    hkbSenseHandleModifier::hkbSenseHandleModifier(const hkbSenseHandleModifier& mod)
    :   hkbModifier(mod),
        m_localFrameName(mod.m_localFrameName),
        m_sensorLocalFrameName(mod.m_sensorLocalFrameName)
    {
        m_sensorLocalOffset = mod.m_sensorLocalOffset;
        m_ranges            = mod.m_ranges;
        hkString::memCpy(&m_minDistance, &mod.m_minDistance, 0x17);
        m_distanceOut    = 0.0f;
        m_foundHandleOut = false;
        m_handle.clear();
    }

    void hkbSenseHandleModifier::activate(const hkbContext& context)
    {
        m_distanceOut                        = 0.0f;
        m_foundHandleOut                     = false;
        m_timeSinceLastModify                = 0.0f;
        m_rangeIndexForEventToSendNextUpdate = -1;
        m_handle.clear();
    }

    /// @note The range's event is raised one update after modify picked it.
    void hkbSenseHandleModifier::update(const hkbContext& context, hkReal timestep)
    {
        m_timeSinceLastModify += timestep;

        if (m_rangeIndexForEventToSendNextUpdate != -1) {
            hkbCharacter*     character = context.m_character;
            hkbBehaviorGraph* behavior  = character->m_behaviorGraph;
            const Range&      range     = m_ranges[m_rangeIndexForEventToSendNextUpdate];
            hkbEventQueue*    queue     = context.m_eventQueue ? context.m_eventQueue : character->m_eventQueue;
            hkbNode*          sender    = behavior->getNodeTemplate(this);
            hkbEvent          event(range.m_event.m_id, range.m_event.m_payload, sender);
            event.m_id = queue->toExternal(event.m_id);
            queue->enqueue(event);
            m_rangeIndexForEventToSendNextUpdate = -1;
        }
    }

    void hkbSenseHandleModifier::deactivate(const hkbContext& context)
    {
        m_handle.clear();
        m_handleIn  = HK_NULL;
        m_handleOut = HK_NULL;
    }

    bool hkbSenseHandleModifier::isValid(hkStringPtr& errorString) const
    {
        const hkbVariableBindingSet* bindings = m_variableBindingSet;

        const bool usingRagdollBone   = m_sensorRagdollBoneIndex >= 0 || (bindings && bindings->findBindingByMemberPath("sensorRagdollBoneIndex"));
        const bool usingAnimationBone = m_sensorAnimationBoneIndex >= 0 || (bindings && bindings->findBindingByMemberPath("sensorAnimationBoneIndex"));

        if (usingRagdollBone) {
            if (usingAnimationBone) {
                errorString = "You cannot specify both a ragdoll bone and an animation bone for sensing.";
                return false;
            }
        } else if (m_extrapolateSensorPosition) {
            errorString = "You can only extrapolate the sensor position when using a ragdoll bone for sensing.";
            return false;
        }

        for (int i = 0; i < m_ranges.getSize(); i++) {
            if (m_ranges[i].m_minDistance > m_ranges[i].m_maxDistance) {
                errorString = "The minimum distance between the sensor position and the sensed handle cannot be greater than the maximum distance.";
                return false;
            }
            if (m_ranges[i].m_maxDistance <= 0.0f) {
                errorString = "The maximum distance to a handle must be greater than zero.";
                return false;
            }
        }
        return true;
    }

    hkbNode* hkbSenseHandleModifier::cloneNode(hkbBehaviorGraph& rootBehavior) const
    {
        return new hkbSenseHandleModifier(*this);
    }

    /// @note A kept handle (m_keepFirstSensedHandle) skips the search; a sensor bone past the pose finds nothing.
    void hkbSenseHandleModifier::modify(const hkbContext& context, hkbGeneratorOutput& inOut)
    {
        if ((!m_handle.m_rigidBody && !m_handle.m_character) || !m_keepFirstSensedHandle) {
            m_handle.clear();
            if (m_sensorAnimationBoneIndex >= inOut.getNumPoseLocal()) {
                m_foundHandleOut      = false;
                m_timeSinceLastModify = 0.0f;
                return;
            }

            hkVector4 sensorPosWS;
            computeSensorPosWS(context, inOut, sensorPosWS);
            hkReal minDistance, maxDistance;
            computeMinMaxDistances(minDistance, maxDistance);

            if (m_sensingMode == SENSE_IN_GIVEN_LOCAL_FRAME_GROUP)  senseInLocalFrameGroup(sensorPosWS, minDistance, maxDistance, context);
            else if (m_sensingMode >= SENSE_IN_OTHER_CHARACTER_SKELETON) senseInSkeletons(sensorPosWS, minDistance, maxDistance, context);
            else                                                    senseInRigidBodies(sensorPosWS, minDistance, maxDistance, context);
        }

        processSensedHandle();
        m_timeSinceLastModify = 0.0f;
    }

    void hkbSenseHandleModifier::computeMinMaxDistances(hkReal& minDistance, hkReal& maxDistance) const
    {
        maxDistance = m_maxDistance;
        minDistance = m_minDistance;
        const int numRanges = m_ranges.getSize();
        for (int i = 0; i < numRanges; i++) {
            if (m_ranges[i].m_maxDistance > maxDistance) maxDistance = m_ranges[i].m_maxDistance;
            if (m_ranges[i].m_minDistance < minDistance) minDistance = m_ranges[i].m_minDistance;
        }
    }

    void hkbSenseHandleModifier::processSensedHandle()
    {
        if (m_handle.m_rigidBody || m_handle.m_character) {
            if (m_handleOut) m_handleOut->set(m_handle);
            m_foundHandleOut = true;

            const int numRanges = m_ranges.getSize();
            for (int i = 0; i < numRanges; i++) {
                if (m_distanceOut > m_ranges[i].m_minDistance && m_distanceOut <= m_ranges[i].m_maxDistance) {
                    m_rangeIndexForEventToSendNextUpdate = i;
                    if (m_ranges[i].m_ignoreHandle) {
                        if (m_handleOut) m_handleOut->clear();
                        m_foundHandleOut = false;
                    }
                    break;
                }
            }
        } else {
            if (m_handleOut) m_handleOut->clear();
            m_foundHandleOut = false;
        }
    }

    /// @note The animation bone wins over the ragdoll bone; without either the sensor hangs off the character. A ragdoll
    ///       bone with a sensor local frame name moves the sensor to that frame (the nearest one on the body).
    /// @note `(driver == HK_NULL) ? HK_NULL : ...` (the `driver ? ... : HK_NULL` spelling moves a register); the character
    ///       is re-read from the context each time, as vanilla does.
    void hkbSenseHandleModifier::computeSensorPosWS(const hkbContext& context, hkbGeneratorOutput& inOut, hkVector4& sensorPosWS) const
    {
        const hkaSkeleton* skeleton = context.m_character->m_setup->m_animationSkeleton;

        if (m_sensorAnimationBoneIndex >= 0 && m_sensorAnimationBoneIndex < skeleton->m_bones.getSize()) {
            hkQsTransform boneModelSpace;
            hkbGeneratorOutputUtils::getBoneModelSpace(m_sensorAnimationBoneIndex, inOut, skeleton, boneModelSpace);
            sensorPosWS.setTransformedPos(boneModelSpace, m_sensorLocalOffset);
            sensorPosWS.setTransformedPos(*static_cast<const hkQsTransform*>(inOut.getTrackData(hkbGeneratorOutput::TRACK_WORLD_FROM_MODEL)), sensorPosWS);
            return;
        }

        hkbRagdollDriver*   driver  = context.m_character->m_ragdollDriver;
        hkaRagdollInstance* ragdoll = (driver == HK_NULL) ? HK_NULL : driver->getRagdoll();
        if (m_sensorRagdollBoneIndex >= 0 && m_sensorRagdollBoneIndex < ragdoll->getSkeleton()->m_bones.getSize() &&
            ragdoll->getRigidBodyOfBone(m_sensorRagdollBoneIndex)) {
            hkpRigidBody* rigidBody = ragdoll->getRigidBodyOfBone(m_sensorRagdollBoneIndex);
            sensorPosWS.setTransformedPos(rigidBody->getTransform(), m_sensorLocalOffset);

            const char* frameName = m_sensorLocalFrameName.cString();
            if (frameName && rigidBody->getLocalFrame()) {
                hkbClosestLocalFrameCollector collector(frameName);
                collector.setRigidBody(rigidBody);
                rigidBody->getLocalFrame()->getNearbyFrames(m_sensorLocalOffset, HK_REAL_MAX, collector);
                if (collector.m_closestLocalFrame.m_frame) {
                    hkTransform frameTransform;
                    collector.m_closestLocalFrame.m_frame->getTransformToRoot(frameTransform);
                    sensorPosWS.setTransformedPos(frameTransform, m_sensorLocalOffset);
                    sensorPosWS.setTransformedPos(rigidBody->getTransform(), sensorPosWS);
                }
            }

            if (m_extrapolateSensorPosition) sensorPosWS.addMul4(hkSimdReal(m_timeSinceLastModify), rigidBody->getLinearVelocity());
            return;
        }

        sensorPosWS.setTransformedPos(*context.m_character->m_worldFromModel, m_sensorLocalOffset);
    }

    void hkbSenseHandleModifier::getRigidBodiesThatCollide(const hkpGroupFilter* filter, const hkaRagdollInstance* ragdoll, hkUint32 collisionFilterInfo,
                                                           hkArray<hkpRigidBody*>& rigidBodiesOut)
    {
        for (int i = 0; i < ragdoll->m_rigidBodies.getSize(); i++) {
            hkpRigidBody* rigidBody = ragdoll->m_rigidBodies[i];
            if (filter->isCollisionEnabled(collisionFilterInfo, rigidBody->getCollisionFilterInfo())) rigidBodiesOut.pushBack(rigidBody);
        }
    }

    void hkbSenseHandleModifier::collectCharacterLocalFrames(const hkVector4& sensorPosWS, hkReal maxDistance, hkbCharacter* character,
                                                             hkbClosestLocalFrameCollector& collector)
    {
        const hkaSkeleton* skeleton = character->m_setup->m_animationSkeleton;
        if (!skeleton) return;

        for (int i = 0; i < skeleton->m_localFrames.getSize(); i++) {
            const hkInt16* parentIndices = skeleton->m_parentIndices.m_data;
            hkQsTransform  boneModelSpace;
            hkaSkeletonUtils::transformLocalBoneToModelBone(skeleton->m_localFrames[i].m_boneIndex, parentIndices, character->getPoseLocal(), boneModelSpace);
            hkQsTransform worldFromBone;
            worldFromBone.setMul(*character->m_worldFromModel, boneModelSpace);
            hkVector4 sensorPosBS;
            sensorPosBS.setTransformedInversePos(worldFromBone, sensorPosWS);

            collector.setCharacterAndBone(character, static_cast<hkInt16>(skeleton->m_localFrames[i].m_boneIndex));
            skeleton->m_localFrames[i].m_localFrame->getNearbyFrames(sensorPosBS, maxDistance, collector);
        }
    }

    /// @note m_handleIn names the target: a character (its skeleton's frames), a rigid body (its own frame), or both (every
    ///       ragdoll body's frame); only frames in the group of m_handleIn's frame count.
    void hkbSenseHandleModifier::senseInLocalFrameGroup(const hkVector4& sensorPosWS, hkReal minDistance, hkReal maxDistance, const hkbContext& context)
    {
        if (!m_handleIn || !m_handleIn->m_frame || !m_handleIn->m_frame->getGroup()) return;

        hkbClosestLocalFrameCollector collector(m_localFrameName.cString());
        collector.setMinDistance(minDistance);
        collector.setLocalFrameGroup(m_handleIn->m_frame->getGroup());

        hkpRigidBody* rigidBody = m_handleIn->m_rigidBody;
        if (rigidBody) {
            if (!m_handleIn->m_character) {
                hkVector4 sensorPosRS;
                sensorPosRS.setTransformedInversePos(rigidBody->getTransform(), sensorPosWS);
                if (rigidBody->getLocalFrame()) {
                    collector.setRigidBody(rigidBody);
                    rigidBody->getLocalFrame()->getNearbyFrames(sensorPosRS, maxDistance, collector);
                }
            } else {
                hkbRagdollDriver*   driver  = m_handleIn->m_character->m_ragdollDriver;
                hkaRagdollInstance* ragdoll = (driver == HK_NULL) ? HK_NULL : driver->getRagdoll();
                for (int i = 0; i < ragdoll->m_rigidBodies.getSize(); i++) {
                    hkpRigidBody* body = ragdoll->m_rigidBodies[i];
                    hkVector4     sensorPosRS;
                    sensorPosRS.setTransformedInversePos(body->getTransform(), sensorPosWS);
                    if (body->getLocalFrame()) {
                        collector.setRigidBody(body);
                        body->getLocalFrame()->getNearbyFrames(sensorPosRS, maxDistance, collector);
                    }
                }
            }
        } else {
            collectCharacterLocalFrames(sensorPosWS, maxDistance, m_handleIn->m_character, collector);
        }

        if (collector.m_closestLocalFrame.m_frame) {
            m_handle.set(collector.m_closestLocalFrame);
            m_distanceOut = collector.m_closestDistance;
        }
    }

    /// @note With a local frame name the closest frame on the candidates' skeletons wins; without one, the closest
    ///       character (by its world-from-model translation), whatever the distance window.
    /// @note Mode 7 copies the character to a local before the pushBack: vanilla reads it before growing the array.
    void hkbSenseHandleModifier::senseInSkeletons(const hkVector4& sensorPosWS, hkReal minDistance, hkReal maxDistance, const hkbContext& context)
    {
        hkArray<hkbCharacter*> characters;
        switch (m_sensingMode) {
            case SENSE_IN_OTHER_CHARACTER_SKELETON:
                context.m_character->getNearbyCharacters(maxDistance, characters);
                break;
            case SENSE_IN_THIS_CHARACTER_SKELETON: {
                hkbCharacter* character = context.m_character;
                characters.pushBack(character);
                break;
            }
            case SENSE_IN_GIVEN_CHARACTER_SKELETON:
                if (m_handleIn && m_handleIn->m_character) characters.pushBack(m_handleIn->m_character);
                break;
        }

        const char* frameName = m_localFrameName.cString();
        if (frameName && hkString::strLen(frameName) > 0) {
            hkbClosestLocalFrameCollector collector(m_localFrameName.cString());
            collector.setMinDistance(minDistance);
            for (int i = 0; i < characters.getSize(); i++) collectCharacterLocalFrames(sensorPosWS, maxDistance, characters[i], collector);

            if (collector.m_closestLocalFrame.m_frame) {
                m_handle.set(collector.m_closestLocalFrame);
                m_distanceOut = collector.m_closestDistance;
            }
        } else {
            hkReal        closestDistance  = HK_REAL_MAX;
            hkbCharacter* closestCharacter = HK_NULL;
            for (int i = 0; i < characters.getSize(); i++) {
                hkVector4 diff;
                diff.setSub4(sensorPosWS, characters[i]->m_worldFromModel->m_translation);
                const hkReal distance = diff.length3().getReal();
                if (distance < closestDistance) {
                    closestDistance  = distance;
                    closestCharacter = characters[i];
                }
            }
            m_distanceOut        = closestDistance;
            m_handle.m_character = closestCharacter;
        }
    }

    /// @note The per-case `character` local is vanilla's: it keeps the character in a register across getWorld.
    /// @note The candidates are the character's nearby bodies (modes 0–2), a ragdoll's bodies (3, 4; filtered through the
    ///       world's group filter when it has one) or m_handleIn's body (5). A sensed body's owning character comes from its
    ///       PROPERTY_CHARACTER property, read under the world lock.
    void hkbSenseHandleModifier::senseInRigidBodies(const hkVector4& sensorPosWS, hkReal minDistance, hkReal maxDistance, const hkbContext& context)
    {
        hkArray<hkpRigidBody*> rigidBodiesArray;
        hkpRigidBody**         rigidBodies    = HK_NULL;
        int                    numRigidBodies = 0;

        switch (m_sensingMode) {
            case SENSE_IN_NEARBY_RIGID_BODIES: {
                hkbCharacter* character = context.m_character;
                character->getNearbyRigidBodies(sensorPosWS, maxDistance, context.getWorld(), rigidBodiesArray, m_collisionFilterInfo, false, false);
                rigidBodies    = rigidBodiesArray.m_data;
                numRigidBodies = rigidBodiesArray.getSize();
                break;
            }
            case SENSE_IN_RIGID_BODIES_OUTSIDE_THIS_CHARACTER: {
                hkbCharacter* character = context.m_character;
                character->getNearbyRigidBodies(sensorPosWS, maxDistance, context.getWorld(), rigidBodiesArray, m_collisionFilterInfo, true, false);
                rigidBodies    = rigidBodiesArray.m_data;
                numRigidBodies = rigidBodiesArray.getSize();
                break;
            }
            case SENSE_IN_OTHER_CHARACTER_RIGID_BODIES: {
                hkbCharacter* character = context.m_character;
                character->getNearbyRigidBodies(sensorPosWS, maxDistance, context.getWorld(), rigidBodiesArray, m_collisionFilterInfo, true, true);
                rigidBodies    = rigidBodiesArray.m_data;
                numRigidBodies = rigidBodiesArray.getSize();
                break;
            }
            case SENSE_IN_THIS_CHARACTER_RIGID_BODIES: {
                hkbRagdollDriver* driver = context.m_character->m_ragdollDriver;
                if (!driver) break;
                hkaRagdollInstance* ragdoll = driver->getRagdoll();
                if (!ragdoll) break;
                if (context.getWorld()->m_collisionFilter && context.getWorld()->m_collisionFilter->m_type == hkpCollisionFilter::HK_FILTER_GROUP) {
                    getRigidBodiesThatCollide(static_cast<hkpGroupFilter*>(context.getWorld()->m_collisionFilter), ragdoll, m_collisionFilterInfo, rigidBodiesArray);
                    rigidBodies    = rigidBodiesArray.m_data;
                    numRigidBodies = rigidBodiesArray.getSize();
                } else {
                    rigidBodies    = ragdoll->m_rigidBodies.m_data;
                    numRigidBodies = ragdoll->m_rigidBodies.getSize();
                }
                break;
            }
            case SENSE_IN_GIVEN_CHARACTER_RIGID_BODIES: {
                if (!m_handleIn || !m_handleIn->m_character) break;
                hkbRagdollDriver* driver = m_handleIn->m_character->m_ragdollDriver;
                if (!driver) break;
                hkaRagdollInstance* ragdoll = driver->getRagdoll();
                if (!ragdoll) break;
                if (context.getWorld()->m_collisionFilter && context.getWorld()->m_collisionFilter->m_type == hkpCollisionFilter::HK_FILTER_GROUP) {
                    getRigidBodiesThatCollide(static_cast<hkpGroupFilter*>(context.getWorld()->m_collisionFilter), ragdoll, m_collisionFilterInfo, rigidBodiesArray);
                    rigidBodies    = rigidBodiesArray.m_data;
                    numRigidBodies = rigidBodiesArray.getSize();
                } else {
                    rigidBodies    = ragdoll->m_rigidBodies.m_data;
                    numRigidBodies = ragdoll->m_rigidBodies.getSize();
                }
                break;
            }
            case SENSE_IN_GIVEN_RIGID_BODY:
                if (m_handleIn && m_handleIn->m_rigidBody) {
                    rigidBodies    = &m_handleIn->m_rigidBody;
                    numRigidBodies = 1;
                }
                break;
        }

        const char* frameName = m_localFrameName.cString();
        if (frameName && hkString::strLen(frameName) > 0) {
            hkbClosestLocalFrameCollector collector(m_localFrameName.cString());
            collector.setMinDistance(minDistance);
            for (int i = 0; i < numRigidBodies; i++) {
                hkpRigidBody* rigidBody = rigidBodies[i];
                hkVector4     sensorPosRS;
                sensorPosRS.setTransformedInversePos(rigidBody->getTransform(), sensorPosWS);
                if (rigidBody->getLocalFrame()) {
                    collector.setRigidBody(rigidBody);
                    rigidBody->getLocalFrame()->getNearbyFrames(sensorPosRS, maxDistance, collector);
                }
            }

            if (collector.m_closestLocalFrame.m_frame) {
                m_handle.set(collector.m_closestLocalFrame);
                m_distanceOut = collector.m_closestDistance;
            }
        } else {
            hkReal        closestDistance  = HK_REAL_MAX;
            hkpRigidBody* closestRigidBody = HK_NULL;
            for (int i = 0; i < numRigidBodies; i++) {
                hkpRigidBody* rigidBody = rigidBodies[i];
                hkVector4     diff;
                diff.setSub4(sensorPosWS, rigidBody->getPosition());
                const hkReal distance = diff.length3().getReal();
                if (distance < maxDistance && distance < closestDistance) {
                    closestDistance  = distance;
                    closestRigidBody = rigidBody;
                }
            }
            if (closestRigidBody) {
                m_distanceOut        = closestDistance;
                m_handle.m_rigidBody = closestRigidBody;
            }
        }

        if (m_handle.m_rigidBody) {
            context.getWorld()->lock();
            hkbCharacter* character = reinterpret_cast<hkbCharacter*>(m_handle.m_rigidBody->getProperty(PROPERTY_CHARACTER));
            context.getWorld()->unlock();
            m_handle.m_character = character;
        }
    }
} }
