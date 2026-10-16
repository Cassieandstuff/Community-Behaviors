/// @file
/// @brief hkbPoweredRagdollControlsModifier, reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/Modifier/PoweredRagdollControls/hkbPoweredRagdollControlsModifier.h>

#include <Behavior/Modifier/PoweredRagdollControls/hkbPoweredRagdollControlsModifierUtils.h>

namespace CB { namespace schema
{
    /// @note Not in the inventory (only .pdata references it); the TU's first function.
    hkbPoweredRagdollControlsModifier::hkbPoweredRagdollControlsModifier()
    {
        m_userData |= 1;
        m_worldFromModelModeData.m_poseMatchingBone0 = -1;
        m_worldFromModelModeData.m_poseMatchingBone1 = -1;
        m_worldFromModelModeData.m_poseMatchingBone2 = -1;
        m_worldFromModelModeData.m_mode              = hkbWorldFromModelModeData::WORLD_FROM_MODEL_MODE_NONE;
    }

    hkbPoweredRagdollControlsModifier::hkbPoweredRagdollControlsModifier(const hkbPoweredRagdollControlsModifier& mod)
    :   hkbModifier(mod),
        m_controlData(mod.m_controlData)
    {
        m_userData               |= 1;
        m_worldFromModelModeData = mod.m_worldFromModelModeData;
        if (mod.m_boneWeights) {
            if (mod.m_boneWeights->m_variableBindingSet == HK_NULL) {
                m_boneWeights = mod.m_boneWeights;
            } else {
                m_boneWeights.setAndDontIncrementRefCount(new hkbBoneWeightArray(*mod.m_boneWeights));
            }
        }
        if (mod.m_bones) {
            if (mod.m_bones->m_variableBindingSet == HK_NULL) {
                m_bones = mod.m_bones;
            } else {
                m_bones.setAndDontIncrementRefCount(new hkbBoneIndexArray(*mod.m_bones));
            }
        }
    }

    void hkbPoweredRagdollControlsModifier::collectBindables(hkbBindableCollector& collector)
    {
        collector.collectBindable(this);
        if (m_boneWeights) m_boneWeights->collectBindables(collector);
        if (m_bones) m_bones->collectBindables(collector);
    }

    /// @note The pose matching bones may be unset (-1) when a variable binds them; they only matter under
    ///       WORLD_FROM_MODEL_MODE_COMPUTE.
    bool hkbPoweredRagdollControlsModifier::isValid(hkStringPtr& errorString) const
    {
        const hkbVariableBindingSet* bindings = m_variableBindingSet;
        if (m_worldFromModelModeData.m_mode == hkbWorldFromModelModeData::WORLD_FROM_MODEL_MODE_COMPUTE) {
            const bool hasBone0 = m_worldFromModelModeData.m_poseMatchingBone0 >= 0 ||
                                  (bindings && bindings->findBindingByMemberPath("worldFromModelModeData/poseMatchingBone0"));
            const bool hasBone1 = m_worldFromModelModeData.m_poseMatchingBone1 >= 0 ||
                                  (bindings && bindings->findBindingByMemberPath("worldFromModelModeData/poseMatchingBone1"));
            const bool hasBone2 = m_worldFromModelModeData.m_poseMatchingBone2 >= 0 ||
                                  (bindings && bindings->findBindingByMemberPath("worldFromModelModeData/poseMatchingBone2"));
            if (hkUint8(hasBone2 + hasBone1 + hasBone0) < 3) {
                errorString = "You must specify all three pose matching bones.";
                return false;
            }
            const hkbWorldFromModelModeData& data = m_worldFromModelModeData;
            if ((data.m_poseMatchingBone0 != -1 && data.m_poseMatchingBone0 == data.m_poseMatchingBone1) ||
                (data.m_poseMatchingBone1 != -1 && data.m_poseMatchingBone1 == data.m_poseMatchingBone2) ||
                (data.m_poseMatchingBone2 != -1 && data.m_poseMatchingBone2 == data.m_poseMatchingBone0)) {
                errorString = "Each pose matching bone must be unique.";
                return false;
            }
        }

        const bool usingBones       = (m_bones && m_bones->m_boneIndices.getSize() > 0) || (bindings && bindings->findBindingByMemberPath("bones"));
        const bool usingBoneWeights = (m_boneWeights && m_boneWeights->m_boneWeights.getSize() > 0) ||
                                      (bindings && bindings->findBindingByMemberPath("boneWeights"));
        if (usingBones && usingBoneWeights) {
            errorString = "You cannot specify both m_bones and m_boneWeights.  Note that m_boneWeights is deprecated.";
            return false;
        }
        return true;
    }

    hkbNode* hkbPoweredRagdollControlsModifier::cloneNode(hkbBehaviorGraph& rootBehavior) const
    {
        if (m_variableBindingSet == HK_NULL && (m_boneWeights == HK_NULL || m_boneWeights->m_variableBindingSet == HK_NULL) &&
            (m_bones == HK_NULL || m_bones->m_variableBindingSet == HK_NULL)) {
            hkbPoweredRagdollControlsModifier* self = const_cast<hkbPoweredRagdollControlsModifier*>(this);
            self->m_cloneState                      = CLONE_STATE_SHARABLE;
            addReference();
            return self;
        }
        return new hkbPoweredRagdollControlsModifier(*this);
    }

    void hkbPoweredRagdollControlsModifier::modify(const hkbContext& context, hkbGeneratorOutput& inOut)
    {
        hkbPoweredRagdollControlsModifierUtils::modify(*this, inOut);
    }
} }
