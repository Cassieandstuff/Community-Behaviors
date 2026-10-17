#include <Physics/Utilities/CharacterControl/CharacterRigidBody/hkpCharacterRigidBodyListener.h>
#include <Physics/Utilities/CharacterControl/CharacterRigidBody/hkpCharacterRigidBody.h>
#include <Physics/Collide/Agent3/Machine/Nn/hkpAgentNnTrack.h>
#include <Physics/Dynamics/Collide/hkpSimpleConstraintContactMgr.h>
#include <Physics/Dynamics/Entity/hkpRigidBody.h>
#include <Common/Base/Container/LocalArray/hkLocalArray.h>

namespace CB { namespace schema
{
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
} }
