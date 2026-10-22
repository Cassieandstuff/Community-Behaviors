/// @file
/// @brief hkbRigidBodyRagdollControlsModifier, reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/Modifier/RigidBodyRagdollControls/hkbRigidBodyRagdollControlsModifier.h>

#include <Behavior/Modifier/RigidBodyRagdollControls/hkbRigidBodyRagdollControlsModifierUtils.h>
#include <Common/Base/Monitor/hkMonitorStream.h>

namespace CB { namespace havok
{
    /// @note Not in the inventory (only .pdata references it); the TU's first function.
    hkbRigidBodyRagdollControlsModifier::hkbRigidBodyRagdollControlsModifier()
    {
        m_userData |= 1;
    }

    hkbRigidBodyRagdollControlsModifier::hkbRigidBodyRagdollControlsModifier(const hkbRigidBodyRagdollControlsModifier& mod)
    :   hkbModifier(mod),
        m_controlData(mod.m_controlData)
    {
        m_userData |= 1;
        if (mod.m_bones) {
            if (mod.m_bones->m_variableBindingSet == HK_NULL) {
                m_bones = mod.m_bones;
            } else {
                m_bones.setAndDontIncrementRefCount(new hkbBoneIndexArray(*mod.m_bones));
            }
        }
    }

    void hkbRigidBodyRagdollControlsModifier::collectBindables(hkbBindableCollector& collector)
    {
        collector.collectBindable(this);
        if (m_bones) m_bones->collectBindables(collector);
    }

    hkbNode* hkbRigidBodyRagdollControlsModifier::cloneNode(hkbBehaviorGraph& rootBehavior) const
    {
        if (m_variableBindingSet == HK_NULL && (m_bones == HK_NULL || m_bones->m_variableBindingSet == HK_NULL)) {
            hkbRigidBodyRagdollControlsModifier* self = const_cast<hkbRigidBodyRagdollControlsModifier*>(this);
            self->m_cloneState                        = CLONE_STATE_SHARABLE;
            addReference();
            return self;
        }
        return new hkbRigidBodyRagdollControlsModifier(*this);
    }

    void hkbRigidBodyRagdollControlsModifier::modify(const hkbContext& context, hkbGeneratorOutput& inOut)
    {
        HK_TIMER_BEGIN("hkbRigidBodyRagdollControlsModifier");
        hkbRigidBodyRagdollControlsModifierUtils::modify(*this, inOut);
        HK_TIMER_END();
    }
} }
