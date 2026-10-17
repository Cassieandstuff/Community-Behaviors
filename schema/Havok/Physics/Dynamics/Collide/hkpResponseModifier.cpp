/// @file
/// @brief hkpResponseModifier, reimplemented against vanilla (AE 1.6.1170).
#include <Physics/Dynamics/Collide/hkpResponseModifier.h>
#include <Physics/Dynamics/Collide/hkpDynamicsContactMgr.h>
#include <Physics/Dynamics/Constraint/hkpConstraintInstance.h>
#include <Physics/Dynamics/World/Simulation/hkpSimulationIsland.h>
#include <Physics/Dynamics/World/Util/hkpWorldConstraintUtil.h>
#include <Physics/Dynamics/Entity/hkpRigidBody.h>
#include <Common/Base/Monitor/hkMonitorStream.h>

namespace CB { namespace schema
{
    /// @note A new mass changer scales only `body`'s side (the other gets 1s); an existing one gets `body`'s side
    ///       replaced, and both mass factors reset to 1 if both were 0. The unit vector is a literal, not Havok's table.
    void hkpResponseModifier::setInvMassScalingForContact(hkpDynamicsContactMgr* manager, hkpRigidBody* body, hkpConstraintOwner& constraintOwner,
                                                          const hkVector4& factor)
    {
        hkpConstraintInstance* instance = manager->getConstraintInstance();
        if (!instance) return;
        constraintOwner.checkAccessRw();

        HK_TIMER_BEGIN("SetMassChang");
        hkpMassChangerModifierConstraintAtom* massChanger =
            static_cast<hkpMassChangerModifierConstraintAtom*>(hkpWorldConstraintUtil::findModifier(instance, hkpConstraintAtom::TYPE_MODIFIER_MASS_CHANGER));
        if (!massChanger) {
            massChanger = new hkpMassChangerModifierConstraintAtom;
            hkpWorldConstraintUtil::addModifier(instance, constraintOwner, massChanger);
            if (body == instance->getEntityA()) {
                massChanger->m_factorA = factor;
                massChanger->m_factorB.setAll(1.0f);
            } else {
                massChanger->m_factorA.setAll(1.0f);
                massChanger->m_factorB = factor;
            }
        } else {
            if (body == instance->getEntityA()) massChanger->m_factorA = factor;
            else                                massChanger->m_factorB = factor;
            if (massChanger->m_factorA(3) == 0.0f && massChanger->m_factorB(3) == 0.0f) {
                massChanger->m_factorA(3) = 1.0f;
                massChanger->m_factorB(3) = 1.0f;
            }
        }
        HK_TIMER_END();
    }
} }
