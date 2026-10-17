/// @file
/// @brief BSLimbIKModifier, reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/Modifier/BSLimbIK/BSLimbIKModifier.h>

#include <Animation/Rig/hkaPose.h>
#include <Behavior/Character/hkbCharacter.h>
#include <Behavior/Generator/hkbGeneratorOutputUtils.h>
#include <Behavior/Modifier/BSLookAt/BSLookAtModifierUtils.h>
#include <Common/Base/Container/LocalArray/hkLocalArray.h>
#include <Common/Base/Monitor/hkMonitorStream.h>

namespace CB { namespace schema
{
    BSLimbIKModifier::BSLimbIKModifier()
    :   m_limitAngleDegrees(90.0f),
        m_currentAngle(0.0f),
        m_startBoneIndex(-1),
        m_endBoneIndex(-1),
        m_gain(0.5f),
        m_boneRadius(0.8f),
        m_castOffset(3.0f),
        m_timeStep(0.0f),
        m_pSkeletonMemory(HK_NULL)
    {
    }

    BSLimbIKModifier::BSLimbIKModifier(hkFinishLoadedObjectFlag flag)
    :   hkbModifier(flag)
    {
        m_pSkeletonMemory = HK_NULL;
    }

    const void* BSLimbIKModifier::getClassType() const
    {
        return &staticClass();
    }

    void BSLimbIKModifier::activate(const hkbContext& context)
    {
        m_currentAngle = 0.0f;
    }

    void BSLimbIKModifier::deactivate(const hkbContext& context)
    {
        m_timeStep = 0.0f;
    }

    bool BSLimbIKModifier::isValid(hkStringPtr& errorString) const
    {
        if (m_startBoneIndex == -1) {
            errorString.printf("The BSLimbIKModifier requires that the start bone has a valid index.");
            return false;
        }
        if (m_endBoneIndex == -1) {
            errorString.printf("The BSLimbIKModifier requires that the end bone has a valid index.");
            return false;
        }
        return true;
    }

    hkbNode* BSLimbIKModifier::cloneNode(hkbBehaviorGraph& rootBehavior) const
    {
        return new BSLimbIKModifier(*this);
    }

    void BSLimbIKModifier::update(const hkbContext& context, hkReal timestep)
    {
        m_timeStep = timestep;
    }

    BSLimbIKModifier::BSLimbIKModifier(const BSLimbIKModifier& mod)
    :   hkbModifier(mod),
        m_limitAngleDegrees(mod.m_limitAngleDegrees),
        m_currentAngle(mod.m_currentAngle),
        m_startBoneIndex(mod.m_startBoneIndex),
        m_endBoneIndex(mod.m_endBoneIndex),
        m_gain(mod.m_gain),
        m_boneRadius(mod.m_boneRadius),
        m_castOffset(mod.m_castOffset),
        m_timeStep(mod.m_timeStep),
        m_pSkeletonMemory(HK_NULL)
    {
    }
} }
