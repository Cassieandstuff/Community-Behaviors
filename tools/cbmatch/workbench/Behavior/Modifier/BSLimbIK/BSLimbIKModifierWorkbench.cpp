/// @file
/// @brief Workbench: the functions of BSLimbIKModifier.cpp still being matched (tools/cbmatch/parked.yaml says why each is
///        stuck). Compiled only by tools/cbmatch/manifests/workbench.yaml; a body that matches graduates back into
///        havok/Behavior/Modifier/BSLimbIK/BSLimbIKModifier.cpp.
#include <Behavior/Modifier/BSLimbIK/BSLimbIKModifier.h>
#include <Animation/Rig/hkaPose.h>
#include <Behavior/Character/hkbCharacter.h>
#include <Behavior/Generator/hkbGeneratorOutputUtils.h>
#include <Behavior/Modifier/BSLookAt/BSLookAtModifierUtils.h>
#include <Common/Base/Container/LocalArray/hkLocalArray.h>
#include <Common/Base/Monitor/hkMonitorStream.h>

namespace CB { namespace havok
{
    namespace
    {
        /// @brief acos, 0 at or above 1 and pi at or below -1.
        HK_FORCE_INLINE hkReal safeAcos(hkReal r)
        {
            if (hkMath::fabs(r) >= 1.0f) return (r > 0.0f) ? 0.0f : HK_REAL_PI;
            return ::acosf(r);
        }
    }

    /// @note Casts down (against the world's up) through every bone from m_startBoneIndex to m_endBoneIndex and takes the
    ///       bone whose ground hit sits highest into its cast; the start bone then swings (around its parent's -x axis,
    ///       toward m_currentAngle, eased by m_gain) by the angle it sees between that cast's end and the ground point,
    ///       limited to m_limitAngleDegrees. The swing goes to the chain getBoneChain returns, which vanilla asks for from
    ///       the start bone to the start bone (just that bone): reproduced as is.
    void BSLimbIKModifier::modify(const hkbContext& context, hkbGeneratorOutput& inOut)
    {
        const int          numPoseLocal = inOut.getNumPoseLocal();
        const hkaSkeleton* skeleton     = context.m_character->m_setup->m_animationSkeleton;
        HK_TIMER_BEGIN("BSLimbIKModifier");

        if (skeleton && numPoseLocal > 0 && m_startBoneIndex >= 0 && m_endBoneIndex >= 0 &&
            (m_startBoneIndex < numPoseLocal || m_endBoneIndex < numPoseLocal) && BSILimbIKModifierSingleton::s_instance) {
            const int memorySize = hkaPose::getRequiredMemorySize(skeleton);
            char*     memory     = hkAllocateStack<char>(memorySize);
            {
                hkaPose pose(skeleton, memory);
                hkbGeneratorOutputUtils::getFilledPoseLocal(inOut, skeleton, pose.accessUnsyncedPoseLocalSpace().begin());

                hkReal scale = 1.0f;
                if (BSILimbIKModifierSingleton::s_instance) scale = BSILimbIKModifierSingleton::s_instance->getWorldScale();
                const hkReal castOffset = scale * m_castOffset;
                const hkReal castLength = scale * m_boneRadius + castOffset;

                const int numBones = m_endBoneIndex - m_startBoneIndex + 1;
                hkLocalArray<hkVector4> bonePositions(numBones);
                bonePositions.setSize(numBones);
                hkLocalArray<hkVector4> castFrom(numBones);
                castFrom.setSize(numBones);
                hkLocalArray<hkVector4> castTo(numBones);
                castTo.setSize(numBones);

                hkVector4 aabbMin(HK_REAL_MAX, HK_REAL_MAX, HK_REAL_MAX, 0.0f);
                hkVector4 aabbMax(-HK_REAL_MAX, -HK_REAL_MAX, -HK_REAL_MAX, 0.0f);
                hkInt16   j = 0;
                for (hkInt16 i = m_startBoneIndex; i <= m_endBoneIndex; j++, i++) {
                    bonePositions[j].setXYZ(pose.getBoneModelSpace(i).m_translation);
                    bonePositions[j].setTransformedPos(inOut.accessWorldFromModel(), bonePositions[j]);
                    castFrom[j].setXYZ(bonePositions[j]);
                    castFrom[j](2) += castOffset;
                    castTo[j].setXYZ(castFrom[j]);
                    castTo[j].subMul4(hkSimdReal(castLength), context.getWorldUpWS());
                    aabbMax.setMax4(aabbMax, castFrom[j]);
                    aabbMin.setMin4(aabbMin, castTo[j]);
                }

                hkReal                    bestPenetration = -HK_REAL_MAX;
                hkInt16                   bestBone        = -1;
                BSLimbIKModifierCastInfo* castInfo        = HK_NULL;
                if (BSILimbIKModifierSingleton::s_instance && BSILimbIKModifierSingleton::s_instance->startCast(castInfo, context, aabbMin, aabbMax)) {
                    hkVector4 bestCastTo;
                    hkVector4 bestHit;
                    j = 0;
                    for (hkInt16 i = m_startBoneIndex; i <= m_endBoneIndex; j++, i++) {
                        const hkVector4& to          = castTo[j];
                        const hkVector4& from        = castFrom[j];
                        hkReal           hitFraction = HK_REAL_MAX;
                        hkVector4        hitNormal;
                        if (BSILimbIKModifierSingleton::s_instance && BSILimbIKModifierSingleton::s_instance->castRay(castInfo, from, to, hitFraction, hitNormal)) {
                            const hkReal hitZ        = castFrom[j](2) - hitFraction * castLength;
                            const hkReal penetration = hitZ - castTo[j](2);
                            if (penetration > bestPenetration) {
                                bestCastTo      = castTo[j];
                                bestBone        = i;
                                bestPenetration = penetration;
                                bestHit.set(bonePositions[j](0), bonePositions[j](1), hitZ, 0.0f);
                            }
                        }
                    }

                    hkReal targetAngle = 0.0f;
                    if (bestBone != -1) {
                        const hkQsTransform& startModelSpace = pose.getBoneModelSpace(m_startBoneIndex);
                        hkVector4            castToMS;
                        castToMS.setTransformedInversePos(inOut.accessWorldFromModel(), bestCastTo);
                        hkVector4 hitMS;
                        hitMS.setTransformedInversePos(inOut.accessWorldFromModel(), bestHit);
                        hkVector4 toCastTo;
                        toCastTo.setSub4(castToMS, startModelSpace.m_translation);
                        toCastTo.normalize3();
                        hkVector4 toHit;
                        toHit.setSub4(hitMS, startModelSpace.m_translation);
                        toHit.normalize3();
                        targetAngle = hkMath::min2(m_limitAngleDegrees * HK_REAL_DEG_TO_RAD, safeAcos(toCastTo.dot3(toHit).getReal()));
                    }

                    if (targetAngle != 0.0f || m_currentAngle != 0.0f) {
                        const hkReal currentAngle = m_currentAngle;
                        m_currentAngle            = BSComputeGainForTimeStep(m_gain, m_timeStep) * (targetAngle - currentAngle) + currentAngle;
                        if (targetAngle == 0.0f && hkMath::fabs(m_currentAngle) < 0.01f) m_currentAngle = 0.0f;

                        hkArray<hkInt16> chain;
                        if (hkaSkeletonUtils::getBoneChain(*skeleton, m_startBoneIndex, m_startBoneIndex, chain)) {
                            const int    numChainBones  = chain.getSize();
                            hkQuaternion parentRotation = hkQuaternion::getIdentity();
                            const int    parent         = skeleton->m_parentIndices[m_startBoneIndex];
                            if (parent != -1) parentRotation = pose.getBoneModelSpace(parent).m_rotation;

                            const hkReal anglePerBone  = m_currentAngle / hkReal(numChainBones);
                            for (int k = 0; k < numChainBones; k++) {
                                hkQuaternion inverseParent;
                                inverseParent.setInverse(parentRotation);
                                hkVector4 axis;
                                axis.setRotatedDir(inverseParent, hkVector4(-1.0f, 0.0f, 0.0f, 0.0f));
                                axis.normalize3();
                                hkQuaternion delta;
                                delta.setAxisAngle(axis, anglePerBone);

                                const hkQuaternion localRotation = pose.getBoneLocalSpace(chain[k]).m_rotation;
                                hkQuaternion       rotation;
                                rotation.setMul(delta, localRotation);
                                pose.accessBoneLocalSpace(chain[k]).m_rotation = rotation;
                                parentRotation.setMul(parentRotation, rotation);
                            }

                            hkString::memCpy(inOut.accessPoseLocal(), pose.getSyncedPoseLocalSpace().begin(), numPoseLocal * int(sizeof(hkQsTransform)));
                            hkbGeneratorOutputUtils::markPoseDense(inOut);
                        }
                    }
                }
                delete castInfo;
            }
            hkDeallocateStack<char>(memory, memorySize);
        }

        HK_TIMER_END();
    }

} }
