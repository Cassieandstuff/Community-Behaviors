/// @file
/// @brief hkbGetUpModifier, reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/Modifier/GetUp/hkbGetUpModifier.h>

#include <Animation/Mapper/hkaSkeletonMapper.h>
#include <Animation/Ragdoll/Instance/hkaRagdollInstance.h>
#include <Animation/Rig/hkaPoseMatchingUtility.h>
#include <Behavior/Character/hkbCharacter.h>
#include <Behavior/Driver/Ragdoll/hkbRagdollDriver.h>
#include <Behavior/Generator/hkbGeneratorOutputUtils.h>
#include <Behavior/Utils/hkbRotationUtils.h>
#include <Common/Base/Container/LocalArray/hkLocalBuffer.h>

namespace CB { namespace schema
{
    hkbGetUpModifier::hkbGetUpModifier()
    :   m_groundNormal(0.0f, 0.0f, 0.0f, 0.0f),
        m_duration(1.0f),
        m_alignWithGroundDuration(0.0f),
        m_rootBoneIndex(-1),
        m_otherBoneIndex(-1),
        m_anotherBoneIndex(-1),
        m_timeSinceBegin(0.0f),
        m_timeStep(0.0f),
        m_initNextModify(false)
    {
    }

    /// @note The settings (m_groundNormal through m_anotherBoneIndex) copy as one 0x1e-byte block.
    hkbGetUpModifier::hkbGetUpModifier(const hkbGetUpModifier& mod)
    :   hkbModifier(mod),
        m_timeSinceBegin(0.0f),
        m_timeStep(0.0f),
        m_initNextModify(false)
    {
        hkString::memCpy(&m_groundNormal, &mod.m_groundNormal,
                         int(reinterpret_cast<const char*>(&mod.m_anotherBoneIndex + 1) - reinterpret_cast<const char*>(&mod.m_groundNormal)));
    }

    hkbGetUpModifierInternalState::hkbGetUpModifierInternalState(hkFinishLoadedObjectFlag flag) : hkReferencedObject(flag)
    {
    }

    void hkbGetUpModifier::activate(const hkbContext& context)
    {
        m_initNextModify = true;
        m_timeSinceBegin = 0.0f;
        m_timeStep       = 0.0f;
    }

    void hkbGetUpModifier::update(const hkbContext& context, hkReal timestep)
    {
        m_timeSinceBegin += timestep;
        m_timeStep += timestep;
    }

    bool hkbGetUpModifier::isValid(hkStringPtr& errorString) const
    {
        const hkbVariableBindingSet* bindings = m_variableBindingSet;
        const bool boneValid[3] = { m_rootBoneIndex >= 0 || (bindings && bindings->findBindingByMemberPath("rootBoneIndex")),
                                    m_otherBoneIndex >= 0 || (bindings && bindings->findBindingByMemberPath("otherBoneIndex")),
                                    m_anotherBoneIndex >= 0 || (bindings && bindings->findBindingByMemberPath("anotherBoneIndex")) };

        if (boneValid[0] + boneValid[1] + boneValid[2] < 3) {
            errorString = "You must specify all three bone indices.";
            return false;
        }
        if (m_rootBoneIndex >= 0 && m_otherBoneIndex >= 0 && m_anotherBoneIndex >= 0 &&
                (m_rootBoneIndex == m_otherBoneIndex || m_rootBoneIndex == m_anotherBoneIndex || m_otherBoneIndex == m_anotherBoneIndex)) {
            errorString = "You must specify unique bone indices for the pose matching bones.";
            return false;
        }

        const bool alignSet  = m_alignWithGroundDuration > 0.0f || (bindings && bindings->findBindingByMemberPath("alignWithGroundDuration"));
        const bool normalSet = !m_groundNormal.equals4(hkVector4::getZero(), 1e-3f) || (bindings && bindings->findBindingByMemberPath("groundNormal"));
        if (alignSet && !normalSet) {
            errorString = "If m_alignWithGroundDuration is non-zero you should set m_groundNormal.";
            return false;
        }
        return true;
    }

    hkbNode* hkbGetUpModifier::cloneNode(hkbBehaviorGraph& rootBehavior) const
    {
        return new hkbGetUpModifier(*this);
    }

    hkReferencedObject* hkbGetUpModifier::createInternalState()
    {
        return new hkbGetUpModifierInternalState;
    }

    void hkbGetUpModifier::getInternalState(hkReferencedObject& internalState) const
    {
        hkbGetUpModifierInternalState& s = static_cast<hkbGetUpModifierInternalState&>(internalState);
        s.m_timeSinceBegin               = m_timeSinceBegin;
        s.m_timeStep                     = m_timeStep;
        s.m_initNextModify               = m_initNextModify;
    }

    void hkbGetUpModifier::setInternalState(const hkReferencedObject& internalState)
    {
        const hkbGetUpModifierInternalState& s = static_cast<const hkbGetUpModifierInternalState&>(internalState);
        m_timeSinceBegin                       = s.m_timeSinceBegin;
        m_timeStep                             = s.m_timeStep;
        m_initNextModify                       = s.m_initNextModify;
    }
} }
