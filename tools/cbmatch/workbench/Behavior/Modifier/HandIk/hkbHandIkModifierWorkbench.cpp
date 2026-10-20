/// @file
/// @brief Workbench: the functions of hkbHandIkModifier.cpp still being matched (tools/cbmatch/parked.yaml says why each is
///        stuck). Compiled only by tools/cbmatch/manifests/workbench.yaml; a body that matches graduates back into
///        havok/Behavior/Modifier/HandIk/hkbHandIkModifier.cpp.
#include <Behavior/Modifier/HandIk/hkbHandIkModifier.h>
#include <Animation/Rig/hkaPose.h>
#include <Behavior/Character/hkbCharacter.h>
#include <Behavior/Context/hkbContext.h>
#include <Behavior/Generator/hkbGeneratorOutputUtils.h>
#include <Behavior/Modifier/HandIkControls/hkbHandIkControlData.h>
#include <Behavior/Utils/hkbBlendCurveUtils.h>
#include <Behavior/Utils/hkbHandleUtils.h>
#include <Common/Base/Memory/Util/Stats/hkStatisticsCollector.h>
#include <Common/Base/Monitor/hkMonitorStream.h>

namespace CB { namespace havok
{
    /// @warning The early returns skip HK_TIMER_END (vanilla's, reproduced): the begin marker is left unpaired.
    /// @note Each hand reads its controls from hand IK track i (the hand's position in m_hands): a world-space target
    ///       (position w > 0 marks it set, the position divided by w) and normal, taken into model space. A sibling bone
    ///       of the shoulder or elbow keeps its rotation relative to that joint across the solve.
    void hkbHandIkModifier::modify(const hkbContext& context, hkbGeneratorOutput& inOut)
    {
        HK_TIMER_BEGIN("HandIkModifier");

        if (!inOut.isValid(hkbGeneratorOutput::TRACK_POSE)) return;
        const int numPoseLocal = inOut.getTrackHeader(hkbGeneratorOutput::TRACK_POSE).m_numData;
        if (numPoseLocal == 0) return;

        const hkaSkeleton* skeleton = context.m_character->m_setup->m_animationSkeleton;
        hkaPose            pose(skeleton);
        hkbGeneratorOutputUtils::getFilledPoseLocal(inOut, skeleton, pose.accessUnsyncedPoseLocalSpace().begin());

        for (int i = 0; i < m_hands.getSize(); i++) {
            const int wristIndex    = m_hands[i].m_wristIndex;
            const int elbowIndex    = m_hands[i].m_elbowIndex;
            const int shoulderIndex = m_hands[i].m_shoulderIndex;
            if (wristIndex < 0 || elbowIndex < 0 || shoulderIndex < 0 || wristIndex >= numPoseLocal || elbowIndex >= numPoseLocal ||
                shoulderIndex >= numPoseLocal) {
                continue;
            }

            InternalHandData& data  = m_internalHandData[i];
            const int         track = hkbGeneratorOutput::TRACK_HAND_IK_CONTROLS_0 + i;
            if (inOut.isValid(track) && inOut.isTrackOn(track)) {
                const hkbHandIkControlData&            controls = *static_cast<const hkbHandIkControlData*>(inOut.getTrackData(track));
                const hkbGeneratorOutput::TrackHeader& header   = inOut.getTrackHeader(track);
                if (data.m_trackOnFraction > header.m_onFraction) {
                    data.m_desiredTransformOnFraction = 0.0f;
                    data.m_desiredNormalOnFraction    = 0.0f;
                    data.m_trackOnFraction            = header.m_onFraction;
                } else {
                    data.m_desiredTransformOnFraction = controls.m_transformOnFraction;
                    data.m_desiredNormalOnFraction    = controls.m_normalOnFraction;
                    data.m_fadeInDuration             = controls.m_fadeInDuration;
                    data.m_fadeOutDuration            = controls.m_fadeOutDuration;

                    hkVector4          targetPosition = controls.m_targetPosition;
                    const hkQuaternion targetRotation = controls.m_targetRotation;
                    if (controls.m_targetPosition(3) > 0.0f) {
                        const hkReal invW = 1.0f / controls.m_targetPosition(3);
                        targetPosition.mul4(invW);
                        const hkQsTransform targetWS(targetPosition, targetRotation);
                        hkQsTransform       targetMS;
                        targetMS.setMulInverseMul(inOut.accessWorldFromModel(), targetWS);
                        data.m_targetPosition = targetMS.m_translation;
                        data.m_targetRotation = targetMS.m_rotation;
                    }

                    hkVector4 targetNormal = controls.m_targetNormal;
                    targetNormal.normalize3IfNotZero();
                    data.m_targetNormal.setRotatedInverseDir(inOut.accessWorldFromModel().m_rotation, targetNormal);
                    data.m_trackOnFraction = header.m_onFraction;
                }
            } else {
                data.m_desiredTransformOnFraction = 0.0f;
                data.m_desiredNormalOnFraction    = 0.0f;
            }

            hkQuaternion shoulderSiblingFromShoulder = hkQuaternion::getIdentity();
            hkQuaternion elbowSiblingFromElbow       = hkQuaternion::getIdentity();
            if (m_hands[i].m_shoulderSiblingIndex >= 0) {
                const hkQsTransform& sibling = pose.getSyncedPoseLocalSpace()[m_hands[i].m_shoulderSiblingIndex];
                const hkQsTransform& joint   = pose.getSyncedPoseLocalSpace()[shoulderIndex];
                shoulderSiblingFromShoulder.setInverseMul(sibling.m_rotation, joint.m_rotation);
            }
            if (m_hands[i].m_elbowSiblingIndex >= 0) {
                const hkQsTransform& sibling = pose.getSyncedPoseLocalSpace()[m_hands[i].m_elbowSiblingIndex];
                const hkQsTransform& joint   = pose.getSyncedPoseLocalSpace()[elbowIndex];
                elbowSiblingFromElbow.setInverseMul(sibling.m_rotation, joint.m_rotation);
            }

            if (data.m_transformOnFraction > 0.0f) {
                const hkReal fraction = hkbBlendCurveUtils::applyBlendCurve(hkbBlendCurveUtils::BlendCurve(m_fadeInOutCurve), data.m_transformOnFraction);
                solveArm(i, fraction, fraction, fraction, data.m_targetPosition, data.m_targetRotation, pose);
            }
            if (data.m_normalOnFraction > 0.0f) {
                const hkReal fraction = hkbBlendCurveUtils::applyBlendCurve(hkbBlendCurveUtils::BlendCurve(m_fadeInOutCurve), data.m_normalOnFraction);
                alignHandNormal(i, fraction, data.m_targetNormal, inOut.accessWorldFromModel(), pose);
            }

            if (m_hands[i].m_shoulderSiblingIndex >= 0) {
                hkQuaternion rotation;
                rotation.setMulInverse(pose.getSyncedPoseLocalSpace()[shoulderIndex].m_rotation, shoulderSiblingFromShoulder);
                pose.accessSyncedPoseLocalSpace()[m_hands[i].m_shoulderSiblingIndex].m_rotation = rotation;
            }
            if (m_hands[i].m_elbowSiblingIndex >= 0) {
                hkQuaternion rotation;
                rotation.setMulInverse(pose.getSyncedPoseLocalSpace()[elbowIndex].m_rotation, elbowSiblingFromElbow);
                pose.accessSyncedPoseLocalSpace()[m_hands[i].m_elbowSiblingIndex].m_rotation = rotation;
            }
        }

        hkString::memCpy(inOut.accessPoseLocal(), pose.getSyncedPoseLocalSpace().begin(), numPoseLocal * int(sizeof(hkQsTransform)));
        hkbGeneratorOutputUtils::markPoseDense(inOut);

        HK_TIMER_END();
    }

} }
