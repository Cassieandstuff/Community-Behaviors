/// @file
/// @brief BSDirectAtModifier, reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/Modifier/BSDirectAt/BSDirectAtModifierUtils.h>

#include <Behavior/Character/hkbCharacter.h>
#include <Common/Base/Monitor/hkMonitorStream.h>

namespace CB { namespace schema
{
    BSDirectAtModifier::BSDirectAtModifier()
    :   m_directAtTarget(true),
        m_sourceBoneIndex(-1),
        m_startBoneIndex(-1),
        m_endBoneIndex(-1),
        m_limitHeadingDegrees(90.0f),
        m_limitPitchDegrees(90.0f),
        m_offsetHeadingDegrees(0.0f),
        m_offsetPitchDegrees(0.0f),
        m_onGain(0.05f),
        m_offGain(0.05f),
        m_targetLocation(0.0f, 0.0f, 0.0f, 0.0f),
        m_userInfo(0),
        m_directAtCamera(false),
        m_directAtCameraX(0.0f),
        m_directAtCameraY(0.0f),
        m_directAtCameraZ(0.0f),
        m_active(false),
        m_currentHeadingOffset(0.0f),
        m_currentPitchOffset(0.0f),
        m_timeStep(0.0f),
        m_pUnknown(HK_NULL),
        m_hasTarget(false),
        m_directAtTargetLocation(0.0f, 0.0f, 0.0f, 0.0f)
    {
    }

    BSDirectAtModifier::BSDirectAtModifier(hkFinishLoadedObjectFlag flag)
    :   hkbModifier(flag),
        m_boneChainIndices(flag)
    {
        m_pUnknown = HK_NULL;
    }

    BSDirectAtModifier::BSDirectAtModifier(const BSDirectAtModifier& mod)
    :   hkbModifier(mod),
        m_directAtTarget(mod.m_directAtTarget),
        m_sourceBoneIndex(mod.m_sourceBoneIndex),
        m_startBoneIndex(mod.m_startBoneIndex),
        m_endBoneIndex(mod.m_endBoneIndex),
        m_limitHeadingDegrees(mod.m_limitHeadingDegrees),
        m_limitPitchDegrees(mod.m_limitPitchDegrees),
        m_offsetHeadingDegrees(mod.m_offsetHeadingDegrees),
        m_offsetPitchDegrees(mod.m_offsetPitchDegrees),
        m_onGain(mod.m_onGain),
        m_offGain(mod.m_offGain),
        m_targetLocation(mod.m_targetLocation),
        m_userInfo(mod.m_userInfo),
        m_directAtCamera(mod.m_directAtCamera),
        m_directAtCameraX(mod.m_directAtCameraX),
        m_directAtCameraY(mod.m_directAtCameraY),
        m_directAtCameraZ(mod.m_directAtCameraZ),
        m_active(mod.m_active),
        m_currentHeadingOffset(mod.m_currentHeadingOffset),
        m_currentPitchOffset(mod.m_currentPitchOffset),
        m_timeStep(mod.m_timeStep),
        m_pUnknown(HK_NULL),
        m_hasTarget(mod.m_hasTarget),
        m_directAtTargetLocation(mod.m_directAtTargetLocation)
    {
    }

    void BSDirectAtModifier::activate(const hkbContext& context)
    {
        m_active               = m_enable;
        m_currentHeadingOffset = 0.0f;
        m_currentPitchOffset   = 0.0f;
    }

    void BSDirectAtModifier::update(const hkbContext& context, hkReal timestep)
    {
        m_hasTarget = m_directAtTarget;
        m_timeStep  = timestep;
        if (BSDirectAtModifierInterface::s_instance) {
            BSDirectAtModifierInterface::s_instance->getTarget(context, m_hasTarget, m_directAtTargetLocation, m_userInfo);
        }

        const hkaSkeleton* skeleton = context.m_character->m_setup->m_animationSkeleton;
        if (skeleton) {
            m_boneChainIndices.clear();
            hkaSkeletonUtils::getBoneChain(*skeleton, m_startBoneIndex, m_endBoneIndex, m_boneChainIndices);
        }
    }

    void BSDirectAtModifier::deactivate(const hkbContext& context)
    {
        m_active               = false;
        m_currentPitchOffset   = 0.0f;
        m_timeStep             = 0.0f;
        m_currentHeadingOffset = 0.0f;
        m_pUnknown             = HK_NULL;
    }

    hkbNode* BSDirectAtModifier::cloneNode(hkbBehaviorGraph& rootBehavior) const
    {
        return new BSDirectAtModifier(*this);
    }

    void BSDirectAtModifier::modify(const hkbContext& context, hkbGeneratorOutput& inOut)
    {
        HK_TIMER_BEGIN("BSDirectAtModifier");
        const hkQsTransform* worldFromModel = context.m_character->m_worldFromModel;
        const hkaSkeleton*   skeleton       = context.m_character->m_setup->m_animationSkeleton;
        BSDirectAtModifierUtils::modifyInternal(*this, skeleton, *worldFromModel, context.getWorldUpWS(), inOut);
        HK_TIMER_END();
    }
} }
