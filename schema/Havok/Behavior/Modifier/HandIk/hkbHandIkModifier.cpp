/// @file
/// @brief hkbHandIkModifier, reimplemented against vanilla (AE 1.6.1170).
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

namespace CB { namespace schema
{
    namespace
    {
        /// @brief Moves `current` toward `desired`: by timestep/fadeInDuration when rising, timestep/fadeOutDuration when
        ///        falling, straight to it for a zero duration.
        HK_FORCE_INLINE void fadeFraction(hkReal& current, hkReal desired, hkReal timestep, hkReal fadeInDuration, hkReal fadeOutDuration)
        {
            const hkReal delta = desired - current;
            if (delta > 0.0f) {
                if (fadeInDuration == 0.0f) current = desired;
                else                        current = hkMath::min2(current + timestep / fadeInDuration, desired);
            } else if (delta < 0.0f) {
                if (fadeOutDuration == 0.0f) current = desired;
                else                         current = hkMath::max2(current - timestep / fadeOutDuration, desired);
            }
        }
    }

    hkbHandIkModifier::hkbHandIkModifier()
    :   m_fadeInOutCurve(hkbBlendCurveUtils::BLEND_CURVE_SMOOTH)
    {
    }

    hkbHandIkModifier::hkbHandIkModifier(const hkbHandIkModifier& mod)
    :   hkbModifier(mod)
    {
        m_hands = mod.m_hands;
    }

    void hkbHandIkModifier::calcContentStatistics(hkStatisticsCollector* collector, const hkClass* cls) const
    {
        hkStatisticsCollectorUtil::addDeclaredMembers(this, &hkbHandIkModifierClass, collector);
        collector->addArray("InternalHandsData", m_internalHandData, hkStatisticsCollector::MEMORY_RUNTIME);
        for (int i = 0; i < m_internalHandData.getSize(); i++) collector->addNormalChunk("InternalHandData", &m_internalHandData[i], sizeof(InternalHandData));
        hkbModifier::calcContentStatistics(collector, &hkbModifierClass);
    }

    /// @note The whole state is zeroed (handle vtables included) before each handle is cleared and looked up: vanilla's.
    void hkbHandIkModifier::activate(const hkbContext& context)
    {
        m_internalHandData.setSize(m_hands.getSize());
        hkString::memSet4(m_internalHandData.begin(), 0, int(m_hands.getSize() * sizeof(InternalHandData) / sizeof(hkUint32)));
        const int numHands = m_hands.getSize();
        for (int i = 0; i < numHands; i++) {
            m_internalHandData[i].m_handle.clear();
            hkbHandleUtils::getHandleFromLocalFrameName(context, m_hands[i].m_localFrameName.cString(), m_internalHandData[i].m_handle);
        }
    }

    void hkbHandIkModifier::update(const hkbContext& context, hkReal timestep)
    {
        const int numHands = m_internalHandData.getSize();
        for (int i = 0; i < numHands; i++) {
            InternalHandData& data = m_internalHandData[i];
            fadeFraction(data.m_transformOnFraction, data.m_desiredTransformOnFraction, timestep, data.m_fadeInDuration, data.m_fadeOutDuration);
            fadeFraction(data.m_normalOnFraction, data.m_desiredNormalOnFraction, timestep, data.m_fadeInDuration, data.m_fadeOutDuration);
        }
    }

    bool hkbHandIkModifier::isValid(hkStringPtr& errorString) const
    {
        if (m_hands.getSize() > 4) {
            errorString = "You have exceeded the maximum number of hands.";
            return false;
        }

        const hkbVariableBindingSet* bindings = m_variableBindingSet;
        for (int i = 0; i < m_hands.getSize(); i++) {
            char shoulderPath[32];
            hkString::sprintf(shoulderPath, "hands:%d/shoulderIndex", i);
            char elbowPath[32];
            hkString::sprintf(elbowPath, "hands:%d/elbowIndex", i);
            char wristPath[32];
            hkString::sprintf(wristPath, "hands:%d/wristIndex", i);

            const bool shoulderOk = m_hands[i].m_shoulderIndex >= 0 || (bindings && bindings->findBindingByMemberPath(shoulderPath));
            const bool elbowOk    = m_hands[i].m_elbowIndex >= 0 || (bindings && bindings->findBindingByMemberPath(elbowPath));
            const bool wristOk    = m_hands[i].m_wristIndex >= 0 || (bindings && bindings->findBindingByMemberPath(wristPath));
            if (!shoulderOk || !elbowOk || !wristOk) {
                errorString = "You must specify all three bone indices.";
                return false;
            }

            const Hand& hand = m_hands[i];
            if (hand.m_shoulderIndex >= 0 && hand.m_elbowIndex >= 0 && hand.m_wristIndex >= 0 &&
                (hand.m_shoulderIndex == hand.m_elbowIndex || hand.m_shoulderIndex == hand.m_wristIndex || hand.m_elbowIndex == hand.m_wristIndex)) {
                errorString = "You must specify unique bone indices for the three bones.";
                return false;
            }
            if (!m_hands[i].m_elbowAxisLS.isNormalized3()) {
                errorString = "Elbow axis must be normalized.";
                return false;
            }
            if (!m_hands[i].m_backHandNormalLS.isNormalized3()) {
                errorString = "Back hand normal must be normalized.";
                return false;
            }
        }
        return true;
    }

    hkbNode* hkbHandIkModifier::cloneNode(hkbBehaviorGraph& rootBehavior) const
    {
        return new hkbHandIkModifier(*this);
    }

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

    void hkbHandIkModifier::alignHandNormal(int handIndex, hkReal fraction, const hkVector4& targetNormalMS, const hkQsTransform& worldFromModel,
                                            hkaPose& pose)
    {
        const int          wristIndex    = m_hands[handIndex].m_wristIndex;
        const hkQuaternion wristRotation = pose.getBoneModelSpace(wristIndex).m_rotation;

        hkVector4 backHandNormalMS;
        backHandNormalMS.setRotatedDir(wristRotation, m_hands[handIndex].m_backHandNormalLS);
        const hkReal cosAngle = backHandNormalMS.dot3(targetNormalMS).getReal();

        hkQuaternion rotation;
        rotation.setIdentity();
        if (cosAngle - 1.0f < -HK_REAL_EPSILON) {
            hkReal angle;
            if (hkMath::fabs(cosAngle) >= 1.0f) angle = (cosAngle > 0.0f) ? 0.0f : HK_REAL_PI;
            else                                angle = ::acosf(cosAngle);

            hkVector4 axis;
            axis.setCross(backHandNormalMS, targetNormalMS);
            if (axis.length3().getReal() > 1e-4f) {
                axis.normalize3();
                rotation.setAxisAngle(axis, angle * fraction);
            }
        }

        hkQuaternion newRotation;
        newRotation.setMul(rotation, wristRotation);
        newRotation.normalize();
        pose.accessBoneModelSpace(wristIndex, hkaPose::PROPAGATE).m_rotation = newRotation;
    }
} }
