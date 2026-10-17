/// @file
/// @brief BSOffsetAnimationGeneratorUtils, reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/Generator/BSOffsetAnimation/BSOffsetAnimationGeneratorUtils.h>

namespace CB { namespace schema
{
    namespace BSOffsetAnimationGeneratorUtils
    {
        void applyOffsets(BSOffsetAnimationGenerator* gen, const hkArray<hkQsTransform>& offsets, const hkArray<hkInt16>& boneIndices,
                          const hkbGeneratorOutput** activeChildrenOutput, hkbGeneratorOutput& output)
        {
            const int numPoseLocal = output.getNumPoseLocal();
            if (!gen->m_bZeroOffset && gen->m_bOffsetValid && numPoseLocal > 0) {
                hkQsTransform* poseLocal = output.accessPoseLocal();
                hkBitField     partitionsSampled(reinterpret_cast<hkUint32*>(poseLocal + numPoseLocal), numPoseLocal + 1);
                if (poseLocal) {
                    for (int i = 0; i < boneIndices.getSize(); i++) {
                        const int bone = boneIndices[i];
                        if (bone >= 0 && bone < numPoseLocal && partitionsSampled.get(bone)) {
                            hkQsTransform& bonePose = poseLocal[bone];
                            hkQsTransform  offsetPose;
                            offsetPose.setMul(offsets[i], bonePose);
                            bonePose = offsetPose;
                        }
                    }
                }
            }
        }
    }
} }
