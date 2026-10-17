#include <Physics/Utilities/CharacterControl/CharacterRigidBody/hkpCharacterRigidBody.h>
#include <Physics/Collide/Agent3/Machine/Nn/hkpAgentNnTrack.h>
#include <Physics/ConstraintSolver/Simplex/hkpSimplexSolver.h>
#include <Physics/Dynamics/Collide/hkpSimpleConstraintContactMgr.h>
#include <Physics/Dynamics/Entity/hkpRigidBody.h>
#include <Common/Base/Container/LocalArray/hkLocalArray.h>
#include <Common/Base/Monitor/hkMonitorStream.h>
#include <Common/Base/Types/Physics/hkStepInfo.h>

namespace CB { namespace schema
{
    int hkpCharacterRigidBody::getSupportInfo(const hkStepInfo& stepInfo, hkArray<SupportInfo>& supportInfo) const
    {
        HK_TIMER_BEGIN("getSupportInfo");

        hkLocalArray<hkSurfaceConstraintInfo> constraints(20);
        hkLocalArray<SupportInfo>             candidates(20);
        hkLocalArray<hkpLinkedCollidable::CollisionEntry> collisionEntries(m_character->getLinkedCollidable()->getCollisionEntriesNonDeterministic().getSize());
        m_character->getLinkedCollidable()->getCollisionEntriesSorted(collisionEntries);

        int state = hkpSurfaceInfo::SUPPORTED;
        for (int i = 0; i < collisionEntries.getSize(); i++) {
            hkpContactMgr* contactMgr = collisionEntries[i].m_agentEntry->m_contactMgr;
            if (contactMgr->m_type != hkpContactMgr::TYPE_SIMPLE_CONSTRAINT_CONTACT_MGR) continue;
            hkpSimpleConstraintContactMgr* mgr = static_cast<hkpSimpleConstraintContactMgr*>(contactMgr);

            const hkpModifierConstraintAtom* modifiers = static_cast<const hkpModifierConstraintAtom*>(mgr->m_constraint.m_constraintModifiers);
            if (modifiers && modifiers->m_type == hkpConstraintAtom::TYPE_MODIFIER_IGNORE_CONSTRAINT) continue;

            for (int j = 0; j < mgr->m_contactConstraintAtom->m_numContactPoints; j++) {
                const hkContactPointId id = static_cast<hkContactPointId>(mgr->findContactPointId(j));
                if (mgr->getContactPointPropertiesInline(id)->isDisabled()) continue;

                const hkpAgentNnEntry* agentEntry = collisionEntries[i].m_agentEntry;
                const hkpCollidable*   other      = agentEntry->m_collidable[0];
                const bool             isA        = (other == m_character->getCollidable());
                if (isA) other = agentEntry->m_collidable[1];
                hkpRigidBody* otherBody = hkpGetRigidBody(other);
                if (!otherBody) continue;

                SupportInfo& support = candidates.expandOne();
                support.m_rigidBody  = otherBody;
                support.m_point      = mgr->getContactPointInline(id);
                if (!isA) support.m_point.flip();

                hkSurfaceConstraintInfo& constraint = constraints.expandOne();
                constraint.m_plane = support.m_point.m_separatingNormal;
                constraint.m_velocity.setZero4();
                constraint.m_staticFriction          = 0.0f;
                constraint.m_extraUpStaticFriction   = 0.0f;
                constraint.m_extraDownStaticFriction = 0.0f;
                constraint.m_dynamicFriction         = 0.0f;
                switch (otherBody->getMotionType()) {
                    case hkpMotion::MOTION_KEYFRAMED: constraint.m_priority = 1; break;
                    case hkpMotion::MOTION_FIXED:     constraint.m_priority = 2; break;
                    default:                          constraint.m_priority = 0; break;
                }
            }
        }

        const int numConstraints = constraints.getSize();
        hkLocalArray<hkSurfaceConstraintInteraction> interactions(numConstraints);

        hkSimplexSolverInput input;
        input.m_position.setZero4();
        input.m_constraints    = constraints.begin();
        input.m_numConstraints = numConstraints;
        input.m_velocity.setMul4(hkSimdReal(-(m_supportDistance / stepInfo.m_deltaTime)), m_up);
        input.m_maxSurfaceVelocity.setAll(m_maxSpeedForSimplexSolver);
        input.m_upVector     = m_up;
        input.m_deltaTime    = stepInfo.m_deltaTime;
        input.m_minDeltaTime = stepInfo.m_deltaTime;

        hkSimplexSolverOutput output;
        output.m_planeInteractions = interactions.begin();
        interactions.setSize(numConstraints);
        constraints.setSize(numConstraints);
        hkSimplexSolverSolve(input, output);

        if (output.m_velocity.equals3(input.m_velocity, 0.001f)) {
            state = hkpSurfaceInfo::UNSUPPORTED;
        } else {
            const hkReal velocityLengthSquared = output.m_velocity.lengthSquared3().getReal();
            if (velocityLengthSquared >= 0.001f) {
                const hkReal velocityDotUp = output.m_velocity.dot3(m_up).getReal();
                state = (velocityLengthSquared * m_maxSlopeCosine * m_maxSlopeCosine > velocityLengthSquared - velocityDotUp * velocityDotUp)
                            ? hkpSurfaceInfo::SLIDING
                            : hkpSurfaceInfo::SUPPORTED;
            }

            int       numSupporting = 0;
            const int numSolved     = constraints.getSize();
            for (int i = 0; i < numSolved; i++) {
                if (interactions[i].m_touched && (constraints[i].m_plane.dot3(m_up).getReal() > 0.08f)) {
                    supportInfo.expandOne() = candidates[i];
                    numSupporting++;
                }
            }
            if (numSupporting == 0) state = hkpSurfaceInfo::UNSUPPORTED;
        }

        HK_TIMER_END();
        return state;
    }
} }
