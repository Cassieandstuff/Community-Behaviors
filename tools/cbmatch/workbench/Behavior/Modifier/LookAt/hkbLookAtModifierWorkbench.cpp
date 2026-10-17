/// @file
/// @brief Workbench: the functions of hkbLookAtModifier.cpp still being matched (tools/cbmatch/parked.yaml says why each is
///        stuck). Compiled only by tools/cbmatch/manifests/workbench.yaml; a body that matches graduates back into
///        schema/Havok/Behavior/Modifier/LookAt/hkbLookAtModifier.cpp.
#include <Behavior/Modifier/LookAt/hkbLookAtModifier.h>
#include <Animation/Ik/LookAt/hkaLookAtIkSolver.h>
#include <Animation/Rig/hkaPose.h>
#include <Behavior/Character/hkbCharacter.h>
#include <Behavior/Generator/hkbGeneratorOutputUtils.h>

namespace CB { namespace schema
{
    /// @warning The early returns skip HK_TIMER_END (vanilla's, reproduced): the begin marker is left unpaired.
    void hkbLookAtModifier::modify(const hkbContext& context, hkbGeneratorOutput& inOut)
    {
        HK_TIMER_BEGIN("LookAtModifier");

        if (!inOut.isValid(hkbGeneratorOutput::TRACK_POSE)) return;
        const int numPoseLocal = inOut.getTrackHeader(hkbGeneratorOutput::TRACK_POSE).m_numData;
        if (numPoseLocal == 0 || m_headIndex < 0 || m_neckIndex < 0 || m_headIndex >= numPoseLocal || m_neckIndex >= numPoseLocal) return;

        const bool         isOn     = m_isOn;
        const hkaSkeleton* skeleton = context.m_character->m_setup->m_animationSkeleton;
        hkaPose            pose(skeleton);
        hkbGeneratorOutputUtils::getFilledPoseLocal(inOut, skeleton, pose.accessUnsyncedPoseLocalSpace().begin());

        const hkQsTransform& headModelSpace = pose.getBoneModelSpace(m_headIndex);
        const hkQsTransform& neckModelSpace = pose.getBoneModelSpace(m_neckIndex);
        hkQuaternion         neckRotation   = neckModelSpace.m_rotation;
        neckRotation.normalize();

        hkVector4 headForwardMS;
        headForwardMS.setRotatedDir(headModelSpace.m_rotation, m_headForwardLS);
        const hkVector4 targetWS = m_targetWS;

        hkReal desiredWeight = 0.0f;
        hkReal gain;
        if (isOn) {
            hkVector4 targetMS;
            targetMS.setTransformedInversePos(inOut.accessWorldFromModel(), targetWS);
            hkVector4 toTarget;
            toTarget.setSub4(targetMS, headModelSpace.m_translation);
            if (toTarget.dot3(headForwardMS).getReal() >= 0.0f) {
                if (m_lookAtWeight > 0.0f) m_lookAtLastTargetWS.setInterpolate4(m_lookAtLastTargetWS, targetWS, hkSimdReal(m_newTargetGain));
                else                       m_lookAtLastTargetWS = targetWS;
                desiredWeight = 1.0f;
                gain          = m_onGain;
            } else {
                gain = m_offGain;
            }
        } else {
            gain = m_offGain;
        }

        m_lookAtWeight += (desiredWeight - m_lookAtWeight) * gain;
        if (m_lookAtWeight < 0.01f) {
            m_lookAtWeight            = 0.0f;
            m_isTargetInsideLimitCone = false;
        } else {
            hkaLookAtIkSolver::Setup setup;
            setup.m_fwdLS         = m_headForwardLS;
            setup.m_eyePositionLS = m_eyePositionHS;
            setup.m_limitAxisMS.setRotatedDir(neckRotation, m_neckForwardLS);
            setup.m_limitAxisMS.normalize3();
            setup.m_limitAngle = m_limitAngleDegrees * HK_REAL_DEG_TO_RAD;

            hkVector4 targetMS;
            targetMS.setTransformedInversePos(inOut.accessWorldFromModel(), m_lookAtLastTargetWS);

            hkVector4 neckRightMS;
            neckRightMS.setRotatedDir(neckRotation, m_neckRightLS);
            hkaLookAtIkSolver::RangeLimits limits;
            limits.m_limitAngleUp    = m_limitAngleUp * HK_REAL_DEG_TO_RAD;
            limits.m_limitAngleDown  = m_limitAngleDown * HK_REAL_DEG_TO_RAD;
            limits.m_upAxisMS.setCross(neckRightMS, setup.m_limitAxisMS);
            limits.m_limitAngleLeft  = m_limitAngleLeft * HK_REAL_DEG_TO_RAD;
            limits.m_limitAngleRight = m_limitAngleRight * HK_REAL_DEG_TO_RAD;

            m_isTargetInsideLimitCone = hkaLookAtIkSolver::solve(setup, targetMS, m_lookAtWeight, pose.accessBoneModelSpace(m_headIndex, hkaPose::PROPAGATE),
                                                                 m_individualLimitsOn ? &limits : HK_NULL);

            hkString::memCpy(inOut.accessPoseLocal(), pose.getSyncedPoseLocalSpace().begin(), numPoseLocal * int(sizeof(hkQsTransform)));
            hkbGeneratorOutputUtils::markPoseDense(inOut);
        }

        HK_TIMER_END();
    }

} }
