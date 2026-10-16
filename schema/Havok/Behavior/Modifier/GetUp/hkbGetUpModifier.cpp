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

    /// @note The first modify after activate snaps the world-from-model to the ragdoll: the reference frame of the
    ///       three bones in the animation pose (mapped onto the ragdoll skeleton) against the ragdoll's own pose. Every
    ///       modify then turns the model's up axis toward the ground normal (for m_alignWithGroundDuration, unless it is
    ///       zero) and then the world's up (until m_duration), by the share of the time left that this step covers.
    ///       The second test of m_initNextModify, right after clearing it, is vanilla's (it re-reads the flag).
    void hkbGetUpModifier::modify(const hkbContext& context, hkbGeneratorOutput& inOut)
    {
        if (!context.m_character->m_ragdollDriver) return;
        hkaRagdollInstance* ragdoll = context.m_character->m_ragdollDriver->getRagdoll();
        if (!ragdoll) return;

        if (m_initNextModify) {
            const hkaSkeleton* ragdollSkeleton = ragdoll->m_skeleton;
            hkbCharacterSetup* setup           = context.m_character->m_setup;
            const hkInt16      numBones        = hkInt16(ragdollSkeleton->m_bones.getSize());
            if (m_rootBoneIndex >= 0 && m_rootBoneIndex < numBones && m_otherBoneIndex >= 0 && m_otherBoneIndex < numBones && m_anotherBoneIndex >= 0 &&
                m_anotherBoneIndex < numBones && m_rootBoneIndex != m_otherBoneIndex && m_otherBoneIndex != m_anotherBoneIndex &&
                m_anotherBoneIndex != m_rootBoneIndex) {
                const hkaSkeleton*           animationSkeleton = setup->m_animationSkeleton;
                hkLocalBuffer<hkQsTransform> animationPoseModel(animationSkeleton->m_bones.getSize());
                hkbGeneratorOutputUtils::getPoseModelSpace(inOut, animationSkeleton, &animationPoseModel[0]);

                hkLocalBuffer<hkQsTransform> mappedPoseModel(ragdollSkeleton->m_bones.getSize());
                hkaSkeletonUtils::transformLocalPoseToModelPose(ragdollSkeleton->m_bones.getSize(), ragdollSkeleton->m_parentIndices.begin(),
                                                                ragdollSkeleton->m_referencePose.begin(), &mappedPoseModel[0]);
                setup->m_animationToRagdollSkeletonMapper->mapPose(&animationPoseModel[0], ragdollSkeleton->m_referencePose.begin(), &mappedPoseModel[0],
                                                                  hkaSkeletonMapper::CURRENT_POSE);

                hkLocalBuffer<hkQsTransform> ragdollPoseUnscaled(ragdollSkeleton->m_bones.getSize());
                ragdoll->getPoseModelSpace(&ragdollPoseUnscaled[0], hkQsTransform::getIdentity());
                hkLocalBuffer<hkQsTransform> ragdollPoseModel(ragdollSkeleton->m_bones.getSize());
                hkbRagdollDriver::copyAndScalePose(false, ragdollSkeleton->m_bones.getSize(), &ragdollPoseUnscaled[0], &ragdollPoseModel[0], 1.0f);

                hkaPoseMatchingUtility poseMatching(m_rootBoneIndex, m_otherBoneIndex, m_anotherBoneIndex, setup->m_data->m_modelUpMS);
                hkQsTransform          animationWorldFromModel;
                hkQsTransform          ragdollWorldFromModel;
                poseMatching.computeReferenceFrame(&mappedPoseModel[0], &ragdollPoseModel[0], animationWorldFromModel, ragdollWorldFromModel);
                inOut.accessWorldFromModel() = ragdollWorldFromModel;
            }
            m_initNextModify = false;
        }

        if (!m_initNextModify) {
            hkVector4 up;
            hkReal    duration;
            if (m_groundNormal.equals4(hkVector4::getZero(), 1e-3f) || m_alignWithGroundDuration < m_timeSinceBegin) {
                duration = m_duration;
                up       = context.getWorldUpWS();
            } else {
                duration = m_alignWithGroundDuration;
                up       = m_groundNormal;
            }

            hkReal fraction = 1.0f;
            if (duration > m_timeSinceBegin) fraction = m_timeStep / (duration - m_timeSinceBegin);
            if (fraction >= 1.0f) fraction = 1.0f;

            hkQuaternion target;
            target.setIdentity();
            hkQuaternion current = inOut.accessWorldFromModel().m_rotation;
            hkQuaternion rotation;
            hkbRotationUtils::rotateToAlignUp(target, current, up, context.m_character->m_setup->m_data->m_modelUpMS, rotation, fraction,
                                              0.25f * HK_REAL_DEG_TO_RAD);
            inOut.accessWorldFromModel().m_rotation = rotation;
            m_timeStep = 0.0f;
        }
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
