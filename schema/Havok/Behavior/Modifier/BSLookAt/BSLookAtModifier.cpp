/// @file
/// @brief BSLookAtModifier, reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/Modifier/BSLookAt/BSLookAtModifierUtils.h>

#include <Animation/Rig/hkaPose.h>
#include <Behavior/Character/hkbCharacter.h>
#include <Behavior/Generator/hkbGeneratorOutputUtils.h>

namespace CB { namespace schema
{
    BSLookAtModifierBoneData::BSLookAtModifierBoneData()
    :   m_index(-1),
        m_fwdAxisLS(0.0f, 1.0f, 0.0f, 0.0f),
        m_limitAngleDegrees(180.0f),
        m_onGain(0.05f),
        m_offGain(0.05f),
        m_enabled(true),
        m_currentFwdAxisLS(0.0f, 1.0f, 0.0f, 0.0f)
    {
    }

    BSLookAtModifierBoneData::BSLookAtModifierBoneData(hkFinishLoadedObjectFlag flag)
    {
    }

    void BSLookAtModifierBoneData::resetCurrentFwdAxis()
    {
        m_currentFwdAxisLS = m_fwdAxisLS;
    }

    /// @note Both constructors (and the copy) end by clearing m_userData.
    BSLookAtModifier::BSLookAtModifier()
    :   m_lookAtTarget(true),
        m_limitAngleDegrees(90.0f),
        m_limitAngleThresholdDegrees(0.0f),
        m_continueLookOutsideOfLimit(false),
        m_onGain(0.05f),
        m_offGain(0.05f),
        m_useBoneGains(false),
        m_targetLocation(0.0f, 0.0f, 0.0f, 0.0f),
        m_targetOutsideLimits(false),
        m_lookAtCamera(false),
        m_lookAtCameraX(0.0f),
        m_lookAtCameraY(0.0f),
        m_lookAtCameraZ(0.0f),
        m_timeStep(0.0f),
        m_pUnknown(HK_NULL)
    {
        m_userData = 0;
    }

    BSLookAtModifier::BSLookAtModifier(hkFinishLoadedObjectFlag flag)
    :   hkbModifier(flag),
        m_bones(flag),
        m_eyeBones(flag),
        m_targetOutOfLimitEvent(flag)
    {
        m_targetOutsideLimits = false;
        m_pUnknown            = HK_NULL;
        m_userData            = 0;
    }

    BSLookAtModifier::BSLookAtModifier(const BSLookAtModifier& mod)
    :   hkbModifier(mod),
        m_lookAtTarget(mod.m_lookAtTarget),
        m_limitAngleDegrees(mod.m_limitAngleDegrees),
        m_limitAngleThresholdDegrees(mod.m_limitAngleThresholdDegrees),
        m_continueLookOutsideOfLimit(mod.m_continueLookOutsideOfLimit),
        m_onGain(mod.m_onGain),
        m_offGain(mod.m_offGain),
        m_useBoneGains(mod.m_useBoneGains),
        m_targetLocation(mod.m_targetLocation),
        m_targetOutsideLimits(mod.m_targetOutsideLimits),
        m_targetOutOfLimitEvent(mod.m_targetOutOfLimitEvent),
        m_lookAtCamera(mod.m_lookAtCamera),
        m_lookAtCameraX(mod.m_lookAtCameraX),
        m_lookAtCameraY(mod.m_lookAtCameraY),
        m_lookAtCameraZ(mod.m_lookAtCameraZ),
        m_timeStep(mod.m_timeStep),
        m_pUnknown(HK_NULL)
    {
        m_userData = 0;
        m_bones    = mod.m_bones;
        m_eyeBones = mod.m_eyeBones;
    }

    const void* BSLookAtModifier::getClassType() const
    {
        return &staticClass();
    }

    void BSLookAtModifier::activate(const hkbContext& context)
    {
        m_timeStep            = 0.0f;
        m_targetOutsideLimits = false;

        const hkbCharacterSetup* setup    = context.m_character->m_setup;
        const hkaSkeleton*       skeleton = setup ? setup->m_animationSkeleton : hkRefPtr<hkaSkeleton>();
        const int                numBones = skeleton ? skeleton->m_bones.getSize() : 0;

        m_ballBonesValid = true;
        for (int i = 0; i < m_bones.getSize() && m_ballBonesValid; i++) m_ballBonesValid = m_bones[i].m_index < numBones;
        for (int i = 0; i < m_eyeBones.getSize() && m_ballBonesValid; i++) m_ballBonesValid = m_eyeBones[i].m_index < numBones;

        for (int i = 0; i < m_bones.getSize(); i++) m_bones[i].resetCurrentFwdAxis();
        for (int i = 0; i < m_eyeBones.getSize(); i++) m_eyeBones[i].resetCurrentFwdAxis();
    }

    void BSLookAtModifier::update(const hkbContext& context, hkReal timestep)
    {
        m_timeStep = timestep;
    }

    void BSLookAtModifier::deactivate(const hkbContext& context)
    {
        m_pUnknown            = HK_NULL;
        m_targetOutsideLimits = false;
    }

    bool BSLookAtModifier::isValid(hkStringPtr& errorString) const
    {
        bool valid = true;
        if (m_bones.getSize() == 0) {
            errorString.printf("The BSLookAtModifier requires at least one bone in the m_bones array.");
            return false;
        }

        for (int i = 0; i < m_bones.getSize() && valid; i++) {
            if (m_bones[i].m_index == -1) {
                errorString.printf("The BSLookAtModifier requires that all bones have a valid index.");
                valid = false;
            } else if (!m_bones[i].m_fwdAxisLS.isNormalized3()) {
                errorString.printf("The BSLookAtModifier requires that the forward axis for all bones are normalized.");
                valid = false;
            }
        }
        for (int i = 0; i < m_eyeBones.getSize() && valid; i++) {
            if (m_eyeBones[i].m_index == -1) {
                errorString.printf("The BSLookAtModifier requires that all eye bones have a valid index.");
                valid = false;
            } else if (!m_eyeBones[i].m_fwdAxisLS.isNormalized3()) {
                errorString.printf("The BSLookAtModifier requires that the forward axis for all eye bones are normalized.");
                valid = false;
            }
        }
        return valid;
    }

    hkbNode* BSLookAtModifier::cloneNode(hkbBehaviorGraph& rootBehavior) const
    {
        return new BSLookAtModifier(*this);
    }

    void BSLookAtModifier::modify(const hkbContext& context, hkbGeneratorOutput& inOut)
    {
        if (context.m_character && context.m_character->m_setup && context.m_character->m_setup->m_animationSkeleton && m_ballBonesValid &&
            m_bones.getSize() > 0 && inOut.isValid(hkbGeneratorOutput::TRACK_POSE) && inOut.getTrackHeader(hkbGeneratorOutput::TRACK_POSE).m_numData > 0) {
            const int memorySize = hkaPose::getRequiredMemorySize(context.m_character->m_setup->m_animationSkeleton);
            char*     memory     = hkAllocateStack<char>(memorySize);
            {
                hkaPose pose(context.m_character->m_setup->m_animationSkeleton, memory);
                hkQsTransform* poseLocal = pose.accessUnsyncedPoseLocalSpace().begin();
                hkbGeneratorOutputUtils::getFilledPoseLocal(inOut, context.m_character->m_setup->m_animationSkeleton, poseLocal);
                BSLookAtModifierUtils::modifyInternal(*this, pose, inOut, &context);
            }
            hkDeallocateStack<char>(memory, memorySize);
        }
    }
} }
