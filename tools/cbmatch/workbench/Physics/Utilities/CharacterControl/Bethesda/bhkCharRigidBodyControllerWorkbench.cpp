/// @file
/// @brief bhkCharRigidBodyController functions still being matched (see tools/cbmatch/parked.yaml).
#include <Physics/Utilities/CharacterControl/Bethesda/bhkCharRigidBodyController.h>
#include <Physics/Dynamics/Entity/hkpRigidBody.h>

namespace CB { namespace schema
{
    void bhkCharRigidBodyController::characterCallback(hkpWorld* world, hkpCharacterRigidBody* characterRB)
    {
        hkVector4 position;
        GetPositionImpl(position, true);
        const hkQuadReal q = position.m_quad;
        const float x = q.m128_f32[0] * 69.99125f;
        const float y = q.m128_f32[1] * 69.99125f;
        const float z = q.m128_f32[2] * 69.99125f;
        if (!IsFlagSet(17)) {
            bhkCharacterMoveFinishEvent event;
            event.m_position.x = x;
            event.m_position.y = y;
            event.m_position.z = z;
            SendEvent(&event);
        }
        m_flags &= ~0x400u;
        hkpCharacterRigidBodyListener::characterCallback(world, characterRB);
        ProcessCharacterContacts();
    }
} }

#include <Physics/Collide/Agent3/Machine/Nn/hkpAgentNnTrack.h>
#include <Physics/Collide/Agent/hkpProcessCollisionOutput.h>
#include <Physics/Dynamics/Collide/hkpSimpleConstraintContactMgr.h>
#include <Physics/Dynamics/World/hkpWorld.h>

namespace CB { namespace schema
{
    void bhkCharRigidBodyController::considerCollisionEntryForSlope(const hkpWorld* world, hkpCharacterRigidBody* characterRB,
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

            const hkContactPoint*                 cp    = mgr->getContactPoint(id);
            const hkpSimpleContactConstraintAtom* atom  = mgr->m_contactConstraintAtom;
            bool                                  walkable = false;
            const hkpCdBody*                      other = (bodyA == characterRB->getRigidBody()->getCollidable()) ? bodyB : bodyA;
            const hkpContactPointProperties*      props = mgr->getContactPointProperties(id);
            if (!atom) {
                walkable = false;
            } else if (props) {
                const hkUint8   numUserDatas = (other == bodyA) ? atom->m_numUserDatasForBodyA : atom->m_numUserDatasForBodyB;
                const hkUint32* extended     = reinterpret_cast<const hkUint32*>(reinterpret_cast<const hkUint8*>(props) + 0x20);
                const hkUint32* userData     = (other == bodyA) ? &extended[0] : &extended[atom->m_numUserDatasForBodyA];
                const hkUint32  shapeKey     = (numUserDatas && userData) ? *userData : 0xffffffff;
                walkable = !IsSlopeBlocking(bhkGetMeshShape(other->getShape()), true, true, shapeKey);
            }

            hkVector4    normal      = cp->m_separatingNormal;
            const hkReal normalDotUp = characterRB->m_up.dot3(normal).getReal();
            const hkReal signedDotUp = normalDotUp * sign;
            if ((signedDotUp > 0.01f) && (signedDotUp < characterRB->m_maxSlopeCosine) && !walkable) {
                hkpCharacterRigidBody::VertPointInfo info;
                info.m_vertPoint.m_position = cp->m_position;
                info.m_vertPoint.m_position.setInt24W(hkpCharacterRigidBody::VERTICAL_POINT_TAG);
                normal.addMul4(hkSimdReal(-signedDotUp), characterRB->m_up);
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
} }
