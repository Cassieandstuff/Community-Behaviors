/// @file
/// @brief BSDirectAtModifierUtils, reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/Modifier/BSDirectAt/BSDirectAtModifierUtils.h>

#include <Animation/Rig/hkaPose.h>
#include <Behavior/Generator/hkbGeneratorOutput.h>
#include <Behavior/Generator/hkbGeneratorOutputUtils.h>
#include <Behavior/Modifier/BSLookAt/BSLookAtModifierUtils.h>
#include <Common/Base/Container/BitField/hkBitField.h>

namespace CB { namespace schema
{
    void BSDirectAtModifierUtils::modifyInternal(BSDirectAtModifier& modifier, const hkaSkeleton* skeleton, const hkQsTransform& worldFromModel,
                                                 const hkVector4& upWS, hkbGeneratorOutput& inOut)
    {
        const hkVector4 forwardMS(0.0f, 1.0f, 0.0f, 0.0f);
        const int       numPoseLocal = inOut.getNumPoseLocal();
        if (skeleton && modifier.m_startBoneIndex >= 0 && modifier.m_endBoneIndex >= 0 && modifier.m_startBoneIndex < numPoseLocal &&
            modifier.m_endBoneIndex < numPoseLocal) {
            const int memorySize = hkaPose::getRequiredMemorySize(skeleton);
            char*     memory     = hkAllocateStack<char>(memorySize);
            {
                hkaPose pose(skeleton, memory);
                hkbGeneratorOutputUtils::getFilledPoseLocal(inOut, skeleton, pose.accessUnsyncedPoseLocalSpace().begin());

                hkVector4 sourceWS;
                if (modifier.m_sourceBoneIndex == -1) {
                    sourceWS = worldFromModel.m_translation;
                } else {
                    sourceWS.setTransformedPos(inOut.accessWorldFromModel(), pose.getBoneModelSpace(modifier.m_sourceBoneIndex).m_translation);
                }

                hkVector4 targetWS = modifier.m_targetLocation;
                if (modifier.m_hasTarget) targetWS = modifier.m_directAtTargetLocation;
                if (modifier.m_directAtCamera) targetWS.set(modifier.m_directAtCameraX, modifier.m_directAtCameraY, modifier.m_directAtCameraZ);

                bool   active;
                hkReal heading;
                hkReal pitch;
                hkReal gain;
                if (modifier.m_directAtTarget && modifier.m_hasTarget) {
                    active = true;
                    hkVector4 toTarget;
                    toTarget.setSub4(targetWS, sourceWS);
                    toTarget.normalize3();
                    hkVector4 forwardWS;
                    forwardWS.setRotatedDir(worldFromModel.m_rotation, forwardMS);

                    const hkReal headingDelta = -computeHeadingDelta(forwardWS, toTarget);
                    const hkReal pitchDelta   = computePitchDelta(forwardWS, toTarget);
                    const hkReal limitHeading = modifier.m_limitHeadingDegrees * HK_REAL_DEG_TO_RAD;
                    const hkReal limitPitch   = modifier.m_limitPitchDegrees * HK_REAL_DEG_TO_RAD;
                    heading = modifier.m_offsetHeadingDegrees * HK_REAL_DEG_TO_RAD +
                              ((headingDelta < -limitHeading) ? -limitHeading : hkMath::min2(limitHeading, headingDelta));
                    pitch   = modifier.m_offsetPitchDegrees * HK_REAL_DEG_TO_RAD +
                              ((pitchDelta < -limitPitch) ? -limitPitch : hkMath::min2(limitPitch, pitchDelta));
                    gain    = modifier.m_onGain;
                } else {
                    gain    = modifier.m_offGain;
                    active  = false;
                    heading = 0.0f;
                    pitch   = 0.0f;
                }

                const hkReal t = BSComputeGainForTimeStep(gain, modifier.m_timeStep);
                modifier.m_currentHeadingOffset = (heading - modifier.m_currentHeadingOffset) * t + modifier.m_currentHeadingOffset;
                modifier.m_currentPitchOffset   = (pitch - modifier.m_currentPitchOffset) * t + modifier.m_currentPitchOffset;
                if (!active) {
                    if (hkMath::fabs(modifier.m_currentHeadingOffset) < 0.01f) modifier.m_currentHeadingOffset = 0.0f;
                    if (hkMath::fabs(modifier.m_currentPitchOffset) < 0.01f) modifier.m_currentPitchOffset = 0.0f;
                }

                if (modifier.m_currentHeadingOffset != 0.0f || modifier.m_currentPitchOffset != 0.0f) {
                    const int numBones = modifier.m_boneChainIndices.getSize();
                    if (numBones > 0) {
                        hkQuaternion  parentRotation = hkQuaternion::getIdentity();
                        const int     parentIdx      = skeleton->m_parentIndices[modifier.m_startBoneIndex];
                        if (parentIdx != -1) parentRotation = pose.getBoneModelSpace(parentIdx).m_rotation;

                        const hkReal invNumBones = 1.0f / hkReal(numBones);
                        const hkReal headingStep = invNumBones * modifier.m_currentHeadingOffset;
                        const hkReal pitchStep   = invNumBones * modifier.m_currentPitchOffset;
                        hkVector4    rightWS;
                        rightWS.setCross(forwardMS, upWS);

                        for (int i = 0; i < numBones; i++) {
                            hkQuaternion inverseParent;
                            inverseParent.setInverse(parentRotation);

                            hkVector4 upLS;
                            upLS.setRotatedDir(inverseParent, upWS);
                            upLS.normalize3();
                            hkQuaternion headingRotation;
                            headingRotation.setAxisAngle(upLS, headingStep);

                            hkVector4 rightLS;
                            rightLS.setRotatedDir(inverseParent, rightWS);
                            rightLS.normalize3();
                            hkQuaternion pitchRotation;
                            pitchRotation.setAxisAngle(rightLS, pitchStep);

                            hkQuaternion rotation;
                            rotation.setMul(headingRotation, pitchRotation);
                            hkQuaternion boneRotation;
                            boneRotation.setMul(rotation, pose.getBoneLocalSpace(modifier.m_boneChainIndices[i]).m_rotation);
                            pose.accessBoneLocalSpace(modifier.m_boneChainIndices[i]).m_rotation = boneRotation;
                            parentRotation.setMul(parentRotation, boneRotation);
                        }

                        hkString::memCpy(inOut.accessPoseLocal(), pose.getSyncedPoseLocalSpace().begin(), numPoseLocal * int(sizeof(hkQsTransform)));
                        const int numPoseLocalOut = inOut.getNumPoseLocal();
                        hkBitField partition(numPoseLocalOut + 1, reinterpret_cast<hkUint32*>(inOut.accessPoseLocal() + numPoseLocalOut), 1);
                    }
                }
            }
            hkDeallocateStack<char>(memory, memorySize);
        }
    }

    hkReal BSDirectAtModifierUtils::computeHeadingDelta(const hkVector4& from, const hkVector4& to)
    {
        const hkReal toHeading   = hkMath::atan2fApproximation(to.m_quad.m128_f32[0], to.m_quad.m128_f32[1]);
        hkReal       delta       = toHeading - hkMath::atan2fApproximation(from.m_quad.m128_f32[0], from.m_quad.m128_f32[1]);
        if (delta < -HK_REAL_PI) delta += 2.0f * HK_REAL_PI;
        if (delta > HK_REAL_PI) delta -= 2.0f * HK_REAL_PI;
        return delta;
    }

    hkReal BSDirectAtModifierUtils::computePitchDelta(const hkVector4& from, const hkVector4& to)
    {
        const hkReal horizontal = ::sqrtf(to.m_quad.m128_f32[0] * to.m_quad.m128_f32[0] + to.m_quad.m128_f32[1] * to.m_quad.m128_f32[1]);
        const hkReal toPitch    = hkMath::atan2fApproximation(to.m_quad.m128_f32[2], horizontal);
        hkReal       delta      = toPitch - hkMath::atan2fApproximation(from.m_quad.m128_f32[2], horizontal);
        if (delta < -HK_REAL_PI) delta += 2.0f * HK_REAL_PI;
        if (delta > HK_REAL_PI) delta -= 2.0f * HK_REAL_PI;
        return delta;
    }
} }
