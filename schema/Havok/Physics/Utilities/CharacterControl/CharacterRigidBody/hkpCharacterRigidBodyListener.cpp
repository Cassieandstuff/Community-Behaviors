/// @file
/// @brief hkpCharacterRigidBodyListener, reimplemented against vanilla (AE 1.6.1170).
#include <Physics/Utilities/CharacterControl/CharacterRigidBody/hkpCharacterRigidBodyListener.h>
#include <Physics/Utilities/CharacterControl/CharacterRigidBody/hkpCharacterRigidBody.h>
#include <Physics/Collide/Agent3/Machine/Nn/hkpAgentNnTrack.h>
#include <Physics/Collide/Agent/hkpProcessCollisionOutput.h>
#include <Physics/Collide/Shape/Convex/Capsule/hkpCapsuleShape.h>
#include <Physics/Dynamics/Collide/hkpSimpleConstraintContactMgr.h>
#include <Physics/Dynamics/Collide/hkpResponseModifier.h>
#include <Physics/Dynamics/Entity/hkpRigidBody.h>
#include <Physics/Dynamics/World/hkpWorld.h>
#include <Common/Base/Container/LocalArray/hkLocalArray.h>
#include <Common/Base/Monitor/hkMonitorStream.h>

namespace CB { namespace schema
{
    void hkpCharacterRigidBodyListener::characterCallback(hkpWorld* world, hkpCharacterRigidBody* characterRB)
    {
        world->lockCriticalOperations();
        HK_TIMER_BEGIN("characterCallback");
        discardVerticalPoints(characterRB);
        processActualPoints(world, characterRB);
        HK_TIMER_END();
        world->unlockAndAttemptToExecutePendingOperations();
    }

    void hkpCharacterRigidBodyListener::discardVerticalPoints(hkpCharacterRigidBody* characterRB)
    {
        if (characterRB->m_verticalContactPoints.getSize() == 0) return;

        hkArray<hkpLinkedCollidable::CollisionEntry> collisionEntries;
        characterRB->m_character->getLinkedCollidable()->getCollisionEntriesSorted(collisionEntries);
        const int numCollisionEntries = collisionEntries.getSize();
        for (int i = 0; i < numCollisionEntries; i++) {
            hkpContactMgr* contactMgr = collisionEntries[i].m_agentEntry->m_contactMgr;
            if (contactMgr->m_type != hkpContactMgr::TYPE_SIMPLE_CONSTRAINT_CONTACT_MGR) continue;
            hkpSimpleConstraintContactMgr* mgr = static_cast<hkpSimpleConstraintContactMgr*>(contactMgr);

            hkLocalArray<hkContactPointId> contactPointIds(mgr->getMaxNumContactPointIds());
            mgr->getAllContactPointIds(contactPointIds);
            for (int j = characterRB->m_verticalContactPoints.getSize() - 1; j >= 0; j--) {
                const hkpCharacterRigidBody::VertPointInfo& info = characterRB->m_verticalContactPoints[j];
                if (info.m_mgr != mgr) continue;
                const int numContactPoints = contactPointIds.getSize();
                for (int k = 0; k < numContactPoints; k++) {
                    const hkContactPointId id = contactPointIds[k];
                    hkContactPoint*        cp = mgr->getContactPoint(id);
                    const hkQuadReal positionEqual = _mm_cmpeq_ps(cp->m_position.m_quad, info.m_vertPoint.m_position.m_quad);
                    const hkQuadReal normalEqual   = _mm_cmpeq_ps(cp->m_separatingNormal.m_quad, info.m_vertPoint.m_separatingNormal.m_quad);
                    if (_mm_movemask_ps(_mm_and_ps(normalEqual, positionEqual)) == 0xf) {
                        cp->m_position.setInt24W(hkpCharacterRigidBody::DISCARDED_VERTICAL_POINT_TAG);
                        mgr->removeContactPoint(id, *characterRB->m_character->getSimulationIsland());
                        characterRB->m_verticalContactPoints.removeAt(j);
                        contactPointIds.removeAt(k);
                        break;
                    }
                }
            }
        }
        characterRB->m_verticalContactPoints.clear();
    }

    void hkpCharacterRigidBodyListener::processActualPoints(hkpWorld* world, hkpCharacterRigidBody* characterRB)
    {
        hkArray<hkpLinkedCollidable::CollisionEntry> collisionEntries;
        characterRB->m_character->getLinkedCollidable()->getCollisionEntriesSorted(collisionEntries);
        const int numCollisionEntries = collisionEntries.getSize();
        for (int i = 0; i < numCollisionEntries; i++) {
            const hkpLinkedCollidable::CollisionEntry& entry      = collisionEntries[i];
            hkpContactMgr*                             contactMgr = entry.m_agentEntry->m_contactMgr;
            if (contactMgr->m_type != hkpContactMgr::TYPE_SIMPLE_CONSTRAINT_CONTACT_MGR) continue;
            hkpSimpleConstraintContactMgr* mgr = static_cast<hkpSimpleConstraintContactMgr*>(contactMgr);

            hkLocalArray<hkContactPointId> contactPointIds(mgr->getMaxNumContactPointIds());
            mgr->getAllContactPointIds(contactPointIds);
            if (contactPointIds.getSize()) processActualPoints(world, characterRB, entry, mgr, contactPointIds);
        }
    }

    void hkpCharacterRigidBodyListener::processActualPoints(const hkpWorld* world, hkpCharacterRigidBody* characterRB,
                                                            const hkpLinkedCollidable::CollisionEntry& entry, hkpSimpleConstraintContactMgr* mgr,
                                                            hkArray<hkContactPointId>& contactPointIds)
    {
        unweldContactPoints(characterRB, entry, mgr, contactPointIds);
        considerCollisionEntryForSlope(world, characterRB, entry, mgr, contactPointIds);
        considerCollisionEntryForMassModification(world, characterRB, entry, mgr, contactPointIds);
    }

    /// @note The 0.5 is g_quadRealInv2 re-broadcast from lane 0, as vanilla loads and shuffles it. The height threshold
    ///       is taken along the capsule axis before the axis is normalized.
    void hkpCharacterRigidBodyListener::unweldContactPoints(hkpCharacterRigidBody* characterRB, const hkpLinkedCollidable::CollisionEntry& entry,
                                                            hkpSimpleConstraintContactMgr* mgr, const hkArray<hkContactPointId>& contactPointIds)
    {
        hkpRigidBody* body = characterRB->m_character;
        const hkReal  sign = (entry.m_agentEntry->getCollidableA() == body->getCollidable()) ? 1.0f : -1.0f;

        const hkpShape* shape = body->getCollidable()->getShape();
        if (shape->getType() == HK_SHAPE_CAPSULE) {
            const hkpCapsuleShape* capsule = static_cast<const hkpCapsuleShape*>(shape);

            hkVector4 centre;
            centre.setAdd4(capsule->getVertex(0), capsule->getVertex(1));
            centre.setMul4(hkSimdReal(_mm_shuffle_ps(g_quadRealInv2.m_quad, g_quadRealInv2.m_quad, _MM_SHUFFLE(0, 0, 0, 0))), centre);

            hkVector4 toCentre;
            toCentre.setSub4(centre, capsule->getVertex(0));
            const hkReal halfLength = toCentre.length3().getReal();

            hkVector4 axis;
            axis.setSub4(capsule->getVertex(0), centre);
            const hkReal heightThreshold = hkMath::fabs(axis.dot3(characterRB->m_up).getReal()) + characterRB->m_unweldingHeightOffsetFactor * capsule->getRadius();
            axis.mul4(hkSimdReal(1.0f / halfLength));

            const int numContactPoints = contactPointIds.getSize();
            for (int i = 0; i < numContactPoints; i++) {
                hkContactPoint* cp = mgr->getContactPoint(contactPointIds[i]);
                if (cp && (-capsule->getRadius() < cp->getDistance()) && (cp->m_position.getInt24W() != hkpCharacterRigidBody::VERTICAL_POINT_TAG)) {
                    hkVector4 localPos;
                    localPos._setTransformedInversePos(body->getTransform(), cp->m_position);
                    localPos.sub4(centre);
                    if (hkMath::fabs(localPos.dot3(characterRB->m_up).getReal()) < heightThreshold) {
                        const hkReal t = localPos.dot3(axis).getReal();
                        hkVector4 nearest;
                        if (t > halfLength)        nearest = capsule->getVertex(0);
                        else if (t < -halfLength)  nearest = capsule->getVertex(1);
                        else                       nearest.setAddMul4(centre, axis, hkSimdReal(t));

                        hkVector4 nearestWS;
                        nearestWS._setTransformedPos(body->getTransform(), nearest);
                        hkVector4 normal;
                        normal.setSub4(nearestWS, cp->m_position);
                        if (normal.normalize3IfNotZero()) {
                            normal.mul4(hkSimdReal(sign));
                            cp->m_separatingNormal.setXYZ(normal);
                        }
                    }
                }
            }
        }
    }

    /// @note The id is held by reference (vanilla re-reads it from the array after each call). The vertical point is
    ///       built whole in a local, handed to addContactPoint by address, then copied into the list.
    void hkpCharacterRigidBodyListener::considerCollisionEntryForSlope(const hkpWorld* world, hkpCharacterRigidBody* characterRB,
                                                                       const hkpLinkedCollidable::CollisionEntry& entry, hkpSimpleConstraintContactMgr* mgr,
                                                                       hkArray<hkContactPointId>& contactPointIds)
    {
        const hkpCdBody* bodyA = entry.m_agentEntry->getCollidableA();
        const hkpCdBody* bodyB = entry.m_agentEntry->getCollidableB();
        const hkReal     sign  = (hkpGetRigidBody(entry.m_agentEntry->getCollidableA()) == characterRB->m_character) ? 1.0f : -1.0f;

        const int numContactPoints = contactPointIds.getSize();
        for (int i = 0; i < numContactPoints; i++) {
            const hkContactPointId& id = contactPointIds[i];
            if (mgr->getContactPointProperties(id)->isDisabled()) continue;

            const hkContactPoint* cp          = mgr->getContactPoint(id);
            hkVector4             normal      = cp->m_separatingNormal;
            const hkReal          normalDotUp = characterRB->m_up.dot3(normal).getReal();
            const hkReal          signedDotUp = normalDotUp * sign;
            if ((signedDotUp > 0.01f) && (signedDotUp < characterRB->m_maxSlopeCosine)) {
                hkpCharacterRigidBody::VertPointInfo info;
                info.m_vertPoint.m_position = cp->m_position;
                info.m_vertPoint.m_position.setInt24W(hkpCharacterRigidBody::VERTICAL_POINT_TAG);
                normal.addMul4(hkSimdReal(-normalDotUp), characterRB->m_up);
                normal.normalize3();
                info.m_vertPoint.m_separatingNormal = normal;
                info.m_vertPoint.m_separatingNormal(3) = hkMath::min2(cp->getDistance(), 0.0f);
                info.m_mgr = mgr;

                hkpProcessCollisionOutput output(characterRB->m_character->getSimulationIsland());
                const hkContactPointId newId = mgr->addContactPoint(*bodyA, *bodyB, *world->m_collisionInput, output, HK_NULL, info.m_vertPoint);
                if (newId != HK_INVALID_CONTACT_POINT) {
                    characterRB->m_verticalContactPoints.pushBack(info);
                    contactPointIds.pushBack(newId);
                }
            }
        }
    }

    /// @note The motion type is compared as a signed byte (vanilla folds the three tests into a byte range test). The
    ///       signs and the unit factor are vector literals, not Havok's constant table.
    void hkpCharacterRigidBodyListener::considerCollisionEntryForMassModification(const hkpWorld* world, hkpCharacterRigidBody* characterRB,
                                                                                  const hkpLinkedCollidable::CollisionEntry& entry,
                                                                                  hkpSimpleConstraintContactMgr* mgr, const hkArray<hkContactPointId>& contactPointIds)
    {
        const hkpCollidable* collA = entry.m_agentEntry->getCollidableA();
        const hkpCollidable* collB = entry.m_agentEntry->getCollidableB();
        hkpRigidBody*        otherBody;
        hkVector4            sign;
        if (hkpGetRigidBody(collA) == characterRB->m_character) {
            otherBody = hkpGetRigidBody(collB);
            sign.setAll(-1.0f);
        } else {
            otherBody = hkpGetRigidBody(collA);
            sign.setAll(1.0f);
        }

        const hkInt8 motionType = static_cast<hkInt8>(otherBody->getMotionType());
        if (motionType != hkpMotion::MOTION_FIXED && motionType != hkpMotion::MOTION_KEYFRAMED && motionType != hkpMotion::MOTION_CHARACTER) {
            if (characterRB->m_maxForce > 0.0f) {
                hkVector4 normal;
                normal.setZero4();
                const int numContactPoints = contactPointIds.getSize();
                for (int i = 0; i < numContactPoints; i++) normal.add4(mgr->getContactPoint(contactPointIds[i])->m_separatingNormal);
                normal.setMul4(normal, sign);

                const hkReal mass          = characterRB->m_character->getMass();
                const hkReal lengthSquared = normal.lengthSquared3().getReal();
                hkReal       force;
                if (lengthSquared > 0.0f) force = characterRB->m_acceleration.dot3(normal).getReal() * mass / lengthSquared;
                else                      force = mass * characterRB->m_acceleration.length3().getReal();

                hkVector4 factor;
                factor.setAll(1.0f);
                if (force > characterRB->m_maxForce) factor(3) = force / characterRB->m_maxForce;
                hkpResponseModifier::setInvMassScalingForContact(mgr, characterRB->m_character, *characterRB->m_character->getSimulationIsland(), factor);
            } else {
                hkpResponseModifier::setInvMassScalingForContact(mgr, otherBody, *characterRB->m_character->getSimulationIsland(), g_vectorConstantZero);
            }
        }
    }
} }
