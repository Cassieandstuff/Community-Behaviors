/// @file
/// @brief hkbExtractRagdollPoseModifier, reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/Modifier/ExtractRagdollPose/hkbExtractRagdollPoseModifier.h>

#include <Animation/Ragdoll/Instance/hkaRagdollInstance.h>
#include <Behavior/Character/hkbCharacter.h>
#include <Behavior/Context/hkbContext.h>
#include <Behavior/Driver/Ragdoll/hkbRagdollDriver.h>
#include <Physics/Dynamics/Entity/hkpRigidBody.h>

namespace CB { namespace schema
{
    /// @note Not in the inventory (only .pdata references it); the TU's first function.
    hkbExtractRagdollPoseModifier::hkbExtractRagdollPoseModifier()
    :   m_poseMatchingBone0(-1),
        m_poseMatchingBone1(-1),
        m_poseMatchingBone2(-1),
        m_enableComputeWorldFromModel(false)
    {
    }

    hkbExtractRagdollPoseModifier::hkbExtractRagdollPoseModifier(const hkbExtractRagdollPoseModifier& mod)
    :   hkbModifier(mod),
        m_poseMatchingBone0(mod.m_poseMatchingBone0),
        m_poseMatchingBone1(mod.m_poseMatchingBone1),
        m_poseMatchingBone2(mod.m_poseMatchingBone2),
        m_enableComputeWorldFromModel(mod.m_enableComputeWorldFromModel)
    {
    }

    void hkbExtractRagdollPoseModifier::calcContentStatistics(hkStatisticsCollector* collector, const hkClass* cls) const
    {
        hkReferencedObject::calcContentStatistics(collector, cls);
    }

    bool hkbExtractRagdollPoseModifier::isValid(hkStringPtr& errorString) const
    {
        const hkbVariableBindingSet* bindings = m_variableBindingSet;
        if (!m_enableComputeWorldFromModel) {
            if (!bindings || !bindings->findBindingByMemberPath("enableComputeWorldFromModel")) return true;
        }

        const bool hasBone0 = m_poseMatchingBone0 >= 0 || (bindings && bindings->findBindingByMemberPath("poseMatchingBone0"));
        const bool hasBone1 = m_poseMatchingBone1 >= 0 || (bindings && bindings->findBindingByMemberPath("poseMatchingBone1"));
        const bool hasBone2 = m_poseMatchingBone2 >= 0 || (bindings && bindings->findBindingByMemberPath("poseMatchingBone2"));
        const hkUint8 numBones = hkUint8(hasBone2 + hasBone1 + hasBone0);
        if (numBones != 0) {
            if (numBones < 3) {
                errorString = "You must specify all three pose matching bones.";
                return false;
            }
            if ((m_poseMatchingBone0 != -1 && m_poseMatchingBone0 == m_poseMatchingBone1) ||
                (m_poseMatchingBone1 != -1 && m_poseMatchingBone1 == m_poseMatchingBone2) ||
                (m_poseMatchingBone2 != -1 && m_poseMatchingBone2 == m_poseMatchingBone0)) {
                errorString = "Each pose matching bone must be unique.";
                return false;
            }
        }
        return true;
    }

    hkbNode* hkbExtractRagdollPoseModifier::cloneNode(hkbBehaviorGraph& rootBehavior) const
    {
        if (m_variableBindingSet == HK_NULL) {
            hkbExtractRagdollPoseModifier* self = const_cast<hkbExtractRagdollPoseModifier*>(this);
            self->m_cloneState                  = CLONE_STATE_SHARABLE;
            addReference();
            return self;
        }
        return new hkbExtractRagdollPoseModifier(*this);
    }

    void hkbExtractRagdollPoseModifier::modify(const hkbContext& context, hkbGeneratorOutput& inOut)
    {
        hkbRagdollDriver* driver = context.m_character->m_ragdollDriver;
        if (driver) {
            const hkaRagdollInstance* ragdoll = driver->getRagdoll();
            if (ragdoll && ragdoll->m_rigidBodies[0] && ragdoll->m_rigidBodies[0]->getWorld()) {
                driver->extractRagdollPose(context, inOut, m_enableComputeWorldFromModel, m_poseMatchingBone0, m_poseMatchingBone1,
                                           m_poseMatchingBone2);
            }
        }
    }
} }
