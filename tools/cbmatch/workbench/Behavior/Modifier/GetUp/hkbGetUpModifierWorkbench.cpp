/// @file
/// @brief Workbench: the functions of hkbGetUpModifier.cpp still being matched (tools/cbmatch/parked.yaml says why each is
///        stuck). Compiled only by tools/cbmatch/manifests/workbench.yaml; a body that matches graduates back into
///        havok/Behavior/Modifier/GetUp/hkbGetUpModifier.cpp.
#include <Behavior/Modifier/GetUp/hkbGetUpModifier.h>
#include <Animation/Mapper/hkaSkeletonMapper.h>
#include <Animation/Ragdoll/Instance/hkaRagdollInstance.h>
#include <Animation/Rig/hkaPoseMatchingUtility.h>
#include <Behavior/Character/hkbCharacter.h>
#include <Behavior/Driver/Ragdoll/hkbRagdollDriver.h>
#include <Behavior/Generator/hkbGeneratorOutputUtils.h>
#include <Behavior/Utils/hkbRotationUtils.h>
#include <Common/Base/Container/LocalArray/hkLocalBuffer.h>

namespace CB { namespace havok
{
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

} }
