/// @file
/// @brief hkbLookAtModifier, reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/Modifier/LookAt/hkbLookAtModifier.h>

#include <Animation/Ik/LookAt/hkaLookAtIkSolver.h>
#include <Animation/Rig/hkaPose.h>
#include <Behavior/Character/hkbCharacter.h>
#include <Behavior/Generator/hkbGeneratorOutputUtils.h>

namespace CB { namespace havok
{
    hkbLookAtModifier::hkbLookAtModifier()
    :   m_targetWS(hkVector4::getZero()),
        m_headForwardLS(0.0f, 1.0f, 0.0f, 0.0f),
        m_neckForwardLS(0.0f, 1.0f, 0.0f, 0.0f),
        m_neckRightLS(1.0f, 0.0f, 0.0f, 0.0f),
        m_eyePositionHS(0.0f, 0.0f, 0.0f, 0.0f),
        m_newTargetGain(0.2f),
        m_onGain(0.05f),
        m_offGain(0.05f),
        m_limitAngleDegrees(45.0f),
        m_limitAngleLeft(90.0f),
        m_limitAngleRight(-90.0f),
        m_limitAngleUp(30.0f),
        m_limitAngleDown(-30.0f),
        m_headIndex(-1),
        m_neckIndex(-1),
        m_isOn(true),
        m_individualLimitsOn(false),
        m_isTargetInsideLimitCone(false),
        m_lookAtLastTargetWS(hkVector4::getZero()),
        m_lookAtWeight(0.0f)
    {
    }

    /// @note The settings (m_targetWS through m_individualLimitsOn) copy as one 0x76-byte block.
    hkbLookAtModifier::hkbLookAtModifier(const hkbLookAtModifier& mod)
    :   hkbModifier(mod),
        m_isTargetInsideLimitCone(false),
        m_lookAtLastTargetWS(hkVector4::getZero()),
        m_lookAtWeight(0.0f)
    {
        hkString::memCpy(&m_targetWS, &mod.m_targetWS, 0x76);
    }

    void hkbLookAtModifier::activate(const hkbContext& context)
    {
        m_lookAtLastTargetWS.setZero4();
        m_lookAtWeight            = 0.0f;
        m_isTargetInsideLimitCone = false;
    }

    bool hkbLookAtModifier::isValid(hkStringPtr& errorString) const
    {
        const hkbVariableBindingSet* bindings = m_variableBindingSet;
        const bool boneValid[2] = { m_headIndex >= 0 || (bindings && bindings->findBindingByMemberPath("headIndex")),
                                     m_neckIndex >= 0 || (bindings && bindings->findBindingByMemberPath("neckIndex")) };

        if (boneValid[0] + boneValid[1] < 2) {
            errorString = "You must specify both the head and the neck bone.";
            return false;
        }
        return true;
    }

    hkbNode* hkbLookAtModifier::cloneNode(hkbBehaviorGraph& rootBehavior) const
    {
        return new hkbLookAtModifier(*this);
    }

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

    hkReferencedObject* hkbLookAtModifier::createInternalState()
    {
        return new hkbLookAtModifierInternalState;
    }

    void hkbLookAtModifier::getInternalState(hkReferencedObject& internalState) const
    {
        hkbLookAtModifierInternalState& state = static_cast<hkbLookAtModifierInternalState&>(internalState);
        state.m_lookAtLastTargetWS      = m_lookAtLastTargetWS;
        state.m_lookAtWeight            = m_lookAtWeight;
        state.m_isTargetInsideLimitCone = m_isTargetInsideLimitCone;
    }

    void hkbLookAtModifier::setInternalState(const hkReferencedObject& internalState)
    {
        const hkbLookAtModifierInternalState& state = static_cast<const hkbLookAtModifierInternalState&>(internalState);
        m_lookAtLastTargetWS      = state.m_lookAtLastTargetWS;
        m_lookAtWeight            = state.m_lookAtWeight;
        m_isTargetInsideLimitCone = state.m_isTargetInsideLimitCone;
    }

} }
