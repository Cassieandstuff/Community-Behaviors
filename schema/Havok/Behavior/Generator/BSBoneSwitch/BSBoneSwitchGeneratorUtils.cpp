/// @file
/// @brief BSBoneSwitchGeneratorUtils, reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/Generator/BSBoneSwitch/BSBoneSwitchGeneratorUtils.h>

#include <Behavior/Generator/hkbGeneratorOutput.h>

namespace CB { namespace schema
{
    namespace BSBoneSwitchGeneratorUtils
    {
        /// @note The partition bits sit after each output's pose, one per bone plus the whole-pose bit. The children's
        ///       outputs are only read (cast so their bits can be wrapped as an hkBitField).
        void generate(BSBoneSwitchGenerator* gen, const hkbGeneratorOutput** activeChildrenOutput, hkbGeneratorOutput& output)
        {
            const int      numPoseLocal = output.getNumPoseLocal();
            hkQsTransform* poseLocal    = output.accessPoseLocal();
            hkBitField     partitionsSampled(reinterpret_cast<hkUint32*>(poseLocal + numPoseLocal), numPoseLocal + 1);
            if (gen->m_pDefaultGenerator && numPoseLocal > 0 && poseLocal) {
                bool anyCopied = false;
                for (int i = 0; i < gen->m_ChildrenA.getSize(); i++) {
                    hkbGeneratorOutput* childOutput = const_cast<hkbGeneratorOutput*>(activeChildrenOutput[i + 1]);
                    if (childOutput) {
                        hkQsTransform* childPose = childOutput->accessPoseLocal();
                        if (childPose) {
                            const hkbBoneWeightArray* weights = gen->m_ChildrenA[i]->m_spBoneWeight;
                            if (weights && numPoseLocal <= weights->m_boneWeights.getSize()) {
                                const int  childNumPoseLocal = childOutput->getNumPoseLocal();
                                hkBitField childSampled(reinterpret_cast<hkUint32*>(childPose + childNumPoseLocal), childNumPoseLocal + 1);
                                for (int b = 0; b < numPoseLocal; b++) {
                                    if (weights->m_boneWeights[b] > 0.0f && childSampled.get(b)) {
                                        anyCopied                   = true;
                                        poseLocal[b].m_translation = childPose[b].m_translation;
                                        poseLocal[b].m_rotation    = childPose[b].m_rotation;
                                        partitionsSampled.set(b);
                                    }
                                }
                            }
                        }
                    }
                }
                if (anyCopied && !partitionsSampled.get(numPoseLocal)) {
                    bool allSampled = true;
                    for (int b = 0; b < numPoseLocal; b++) allSampled = allSampled && partitionsSampled.get(b);
                    partitionsSampled.assign(numPoseLocal, allSampled);
                }
            }
        }
    }
} }
