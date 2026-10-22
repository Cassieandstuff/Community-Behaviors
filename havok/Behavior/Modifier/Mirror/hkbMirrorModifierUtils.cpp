/// @file
/// @brief hkbMirrorModifierUtils, reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/Modifier/Mirror/hkbMirrorModifierUtils.h>
#include <Behavior/Generator/hkbGeneratorOutputUtils.h>

#include <Behavior/Generator/hkbGeneratorOutput.h>

namespace CB { namespace havok
{
    namespace hkbMirrorModifierUtils
    {
        /// @note Each bone is mirrored in a stack copy of the pose so a partner's slot is never read after it was written.
        void modify(const hkbMirrorModifier& mod, const hkaMirroredSkeleton& mirroredSkeleton, const hkQsTransform* referencePose,
                    const hkQsTransform& worldFromModel, hkbGeneratorOutput& inOut)
        {
            if (inOut.isValid(hkbGeneratorOutput::TRACK_POSE)) {
                const int numPoseLocal = inOut.getTrackHeader(hkbGeneratorOutput::TRACK_POSE).m_numData;
                if (numPoseLocal > 0) {
                    hkbGeneratorOutputUtils::makePoseDense(inOut, referencePose);

                    hkQsTransform* pose = hkAllocateStack<hkQsTransform>(numPoseLocal);
                    hkString::memCpy(pose, inOut.accessPoseLocal(), numPoseLocal * int(sizeof(hkQsTransform)));
                    for (int i = 0; i < numPoseLocal; i++) {
                        mirroredSkeleton.mirrorBone(pose[i], hkInt16(i), mod.m_isAdditive);
                        const hkInt16 pair = mirroredSkeleton.getBonePair(hkInt16(i));
                        if (pair >= 0 && pair < numPoseLocal) inOut.accessPoseLocal()[pair] = pose[i];
                    }
                    hkDeallocateStack(pose, numPoseLocal);
                }
            }

            hkQsTransform& outWorldFromModel = inOut.accessWorldFromModel();
            hkQsTransform  motion;
            motion.setMulInverseMul(worldFromModel, outWorldFromModel);
            mirroredSkeleton.mirrorExtractedMotion(motion);
            inOut.accessWorldFromModel().setMul(worldFromModel, motion);
        }
    }
} }
