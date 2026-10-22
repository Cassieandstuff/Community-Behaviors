/// @file
/// @brief BSLookAtModifierUtils, reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/Modifier/BSLookAt/BSLookAtModifierUtils.h>

#include <Animation/Rig/hkaPose.h>
#include <Behavior/Character/hkbCharacter.h>
#include <Behavior/Event/hkbEventQueue.h>
#include <Behavior/Generator/hkbGeneratorOutput.h>
#include <Common/Base/Container/BitField/hkBitField.h>
#include <Common/Base/Monitor/hkMonitorStream.h>
#include <Common/Base/System/Error/hkError.h>
#include <Common/Base/System/Io/OStream/hkOStream.h>

namespace CB { namespace havok
{
    namespace
    {
        /// @brief acos, 0 at or above 1 and pi at or below -1.
        /// @note Inlined here (hkMath::acos is the out-of-line copy modifyBone also calls).
        HK_FORCE_INLINE hkReal safeAcos(hkReal r)
        {
            if (hkMath::fabs(r) >= 1.0f) return (r > 0.0f) ? 0.0f : HK_REAL_PI;
            return ::acosf(r);
        }
    }

    hkReal BSComputeGainForTimeStep(hkReal gain, hkReal timeStep)
    {
        const hkReal t = gain * timeStep * 30.0f;
        if (t < 0.0f) return 0.0f;
        return hkMath::min2(t, 1.0f);
    }

    /// @note Bones past the output's pose are skipped; the head (last) bone must be inside it or nothing happens.
    ///       The eye bones aim at the unclamped model-space target, which is left unset when the target is outside the
    ///       limits and m_continueLookOutsideOfLimit is off (vanilla's behavior, reproduced; lookAt is then false).
    void BSLookAtModifierUtils::modifyInternal(BSLookAtModifier& modifier, hkaPose& pose, hkbGeneratorOutput& inOut, const hkbContext* context)
    {
        HK_TIMER_BEGIN("BSLookAtModifier");

        const int                       numPoseLocal = inOut.getNumPoseLocal();
        const BSLookAtModifierBoneData& headBone     = modifier.m_bones[modifier.m_bones.getSize() - 1];
        if (headBone.m_index < numPoseLocal) {
            HK_TIMER_BEGIN_LIST("DoModification", "DetermineLookatTarget");

            hkReal    totalAngle = 0.0f;
            hkVector4 targetWS   = modifier.m_targetLocation;
            if (modifier.m_lookAtCamera) targetWS.set(modifier.m_lookAtCameraX, modifier.m_lookAtCameraY, modifier.m_lookAtCameraZ);

            hkVector4 targetMS;
            hkVector4 unclampedTargetMS;
            if (modifier.m_lookAtTarget) {
                const hkReal         limitAngle = modifier.m_limitAngleDegrees * HK_REAL_DEG_TO_RAD;
                const hkQsTransform& headMS     = pose.getBoneModelSpace(headBone.m_index);
                hkVector4            headFwdMS;
                headFwdMS.setRotatedDir(headMS.m_rotation, headBone.m_fwdAxisLS);
                headFwdMS.normalize3();

                hkVector4 toTarget;
                toTarget.setTransformedInversePos(inOut.accessWorldFromModel(), targetWS);
                toTarget.setSub4(toTarget, headMS.m_translation);
                const hkReal distance = toTarget.length3().getReal();
                toTarget.normalize3();
                totalAngle = safeAcos(toTarget.dot3(headFwdMS).getReal());

                if (totalAngle > limitAngle) {
                    if (totalAngle > modifier.m_limitAngleThresholdDegrees * HK_REAL_DEG_TO_RAD + limitAngle) {
                        if (!modifier.m_targetOutsideLimits && modifier.m_targetOutOfLimitEvent.m_id != -1) {
                            if (context) {
                                hkbEventQueue* queue = context->getEventQueue();
                                hkbEvent       event(modifier.m_targetOutOfLimitEvent.m_id, modifier.m_targetOutOfLimitEvent.m_payload,
                                                     &modifier);
                                event.m_id = queue->toExternal(event.m_id);
                                queue->enqueue(event);
                            } else {
                                char        buf[0x200];
                                hkErrStream ostr(buf, sizeof(buf));
                                ostr << "BSLookAtModifierUtils::modifyInternal - ignoring target outside limits event";
                                hkError::getInstance().message(hkError::MESSAGE_WARNING, 0, buf, "Modifier\\BSLookAt\\BSLookAtModifierUtils.cpp", 0x6b);
                            }
                        }
                        modifier.m_targetOutsideLimits = true;
                    }
                } else {
                    modifier.m_targetOutsideLimits = false;
                }

                if (modifier.m_targetOutsideLimits && !modifier.m_continueLookOutsideOfLimit) {
                    totalAngle = 0.0f;
                } else {
                    unclampedTargetMS.setTransformedInversePos(inOut.accessWorldFromModel(), targetWS);
                    if (modifier.m_targetOutsideLimits && modifier.m_continueLookOutsideOfLimit) {
                        hkVector4 up(0.0f, 0.0f, 1.0f, 0.0f);
                        hkVector4 side;
                        side.setCross(headFwdMS, up);
                        side.normalize3();
                        hkReal rotationAngle = limitAngle;
                        if (side.dot3(toTarget).getReal() >= 0.0f) rotationAngle = -rotationAngle;
                        hkQuaternion rotation;
                        rotation.setAxisAngle(up, rotationAngle);

                        hkVector4 flatFwdMS = headFwdMS;
                        flatFwdMS(2)        = 0.0f;
                        flatFwdMS.normalize3();
                        hkVector4 clampedDir;
                        clampedDir.setRotatedDir(rotation, flatFwdMS);
                        clampedDir.normalize3();
                        targetMS.setMul4(clampedDir, hkSimdReal(distance));
                        targetMS.add4(headMS.m_translation);
                        totalAngle = limitAngle;
                    } else {
                        targetMS = unclampedTargetMS;
                    }
                }
            } else {
                modifier.m_targetOutsideLimits = false;
            }

            HK_TIMER_SPLIT_LIST("ApplyLookat");

            int    numEnabledBones = 0;
            hkReal anglePerBone    = 0.0f;
            for (int i = 0; i < modifier.m_bones.getSize(); i++) {
                if (modifier.m_bones[i].m_enabled && modifier.m_bones[i].m_index < numPoseLocal) numEnabledBones++;
            }
            if (numEnabledBones != 0) anglePerBone = totalAngle / hkReal(numEnabledBones);

            const bool lookAt         = modifier.m_lookAtTarget && (!modifier.m_targetOutsideLimits || modifier.m_continueLookOutsideOfLimit);
            int        numBonesDone   = 0;
            int        numBonesToTurn = numEnabledBones;
            for (int i = 0; i < modifier.m_bones.getSize(); i++) {
                if (modifier.m_bones[i].m_index < numPoseLocal) {
                    bool   isLastBone;
                    hkReal limitAngle;
                    if (modifier.m_bones[i].m_enabled && numBonesDone == numEnabledBones - 1) {
                        limitAngle = modifier.m_bones[i].m_limitAngleDegrees * HK_REAL_DEG_TO_RAD;
                        isLastBone = true;
                    } else {
                        limitAngle = hkMath::min2(modifier.m_bones[i].m_limitAngleDegrees * HK_REAL_DEG_TO_RAD, anglePerBone);
                        isLastBone = false;
                    }
                    const hkReal turned = modifyBone(modifier, modifier.m_bones[i], pose, lookAt, targetMS, limitAngle);
                    if (modifier.m_bones[i].m_enabled && !isLastBone) {
                        totalAngle -= turned;
                        numBonesDone++;
                        numBonesToTurn--;
                        anglePerBone = totalAngle / hkReal(numBonesToTurn);
                    }
                }
            }
            for (int i = 0; i < modifier.m_eyeBones.getSize(); i++) {
                if (modifier.m_eyeBones[i].m_index < numPoseLocal) {
                    modifyBone(modifier, modifier.m_eyeBones[i], pose, lookAt, unclampedTargetMS, modifier.m_eyeBones[i].m_limitAngleDegrees * HK_REAL_DEG_TO_RAD);
                }
            }

            HK_TIMER_SPLIT_LIST("CopyPose");
            hkString::memCpy(inOut.accessPoseLocal(), pose.getSyncedPoseLocalSpace().begin(), numPoseLocal * int(sizeof(hkQsTransform)));
            hkBitField partition(numPoseLocal + 1, reinterpret_cast<hkUint32*>(inOut.accessPoseLocal() + numPoseLocal), 1);
            HK_TIMER_END_LIST();
        }

        HK_TIMER_END();
    }

    hkReal BSLookAtModifierUtils::modifyBone(const BSLookAtModifier& modifier, BSLookAtModifierBoneData& bone, hkaPose& pose, bool lookAt,
                                             const hkVector4& targetMS, hkReal limitAngle)
    {
        hkReal angle = 0.0f;
        if (bone.m_index != -1) {
            hkQsTransform& boneMS = pose.accessBoneModelSpace(bone.m_index, hkaPose::PROPAGATE);
            hkVector4      fwdMS;
            fwdMS.setRotatedDir(boneMS.m_rotation, bone.m_fwdAxisLS);
            fwdMS.normalize3();
            hkVector4 currentFwdMS;
            currentFwdMS.setRotatedDir(boneMS.m_rotation, bone.m_currentFwdAxisLS);
            currentFwdMS.normalize3();

            hkVector4 desiredFwdMS = fwdMS;
            if (bone.m_enabled && lookAt) {
                hkVector4 toTarget;
                toTarget.setSub4(targetMS, boneMS.m_translation);
                toTarget.normalize3();
                hkVector4 axis;
                axis.setCross(fwdMS, toTarget);
                axis.normalize3();
                if (hkMath::fabs(axis.length3().getReal() - 1.0f) < 0.01f) {
                    angle = hkMath::acos(toTarget.dot3(fwdMS).getReal());
                    angle = (angle < 0.0f) ? 0.0f : hkMath::min2(angle, limitAngle);
                    hkQuaternion rotation;
                    rotation.setAxisAngle(axis, angle);
                    desiredFwdMS.setRotatedDir(rotation, fwdMS);
                    desiredFwdMS.normalize3();
                }
            }

            hkReal gain;
            if (lookAt) gain = modifier.m_useBoneGains ? bone.m_onGain : modifier.m_onGain;
            else        gain = modifier.m_useBoneGains ? bone.m_offGain : modifier.m_offGain;
            const hkReal t = BSComputeGainForTimeStep(gain, modifier.m_timeStep);

            hkVector4 newFwdMS;
            newFwdMS.setInterpolate4(currentFwdMS, desiredFwdMS, hkSimdReal(t));
            newFwdMS.normalize3();
            hkQuaternion inverseRotation;
            inverseRotation.setInverse(boneMS.m_rotation);
            bone.m_currentFwdAxisLS.setRotatedDir(inverseRotation, newFwdMS);
            bone.m_currentFwdAxisLS.normalize3();

            const hkReal turn = safeAcos(newFwdMS.dot3(fwdMS).getReal());
            if (turn >= 0.001f) {
                hkVector4 turnAxis;
                turnAxis.setCross(fwdMS, newFwdMS);
                turnAxis.normalize3();
                if (hkMath::fabs(turnAxis.length3().getReal() - 1.0f) < 0.01f) {
                    hkQuaternion turnRotation;
                    turnRotation.setAxisAngle(turnAxis, turn);
                    boneMS.m_rotation.setMul(turnRotation, boneMS.m_rotation);
                }
            }
        }
        return angle;
    }
} }
