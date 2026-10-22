/// @file
/// @brief BSSynchronizedClipGeneratorUtils, reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/Generator/BSSynchronizedClipGenerator/BSSynchronizedClipGeneratorUtils.h>

#include <Animation/Rig/hkaSkeleton.h>

namespace CB { namespace havok
{
    namespace BSSynchronizedClipGeneratorUtils
    {
        /// @note Before every character is at its mark, the support character's world transform takes the end mark's
        ///       position with the start mark's rotation and scale, or blends start to end by m_fCurrentLerp when it is
        ///       being reoriented.
        void generate(BSSynchronizedClipGenerator* gen, const hkbContext& context, const hkaSkeleton* skeleton,
                      const hkbGeneratorOutput** activeChildrenOutput, hkbGeneratorOutput& output, hkReal timeOffset)
        {
            if (gen->m_pSyncScene && gen->m_bAllCharactersInScene) {
                if (!gen->m_bAtMark) {
                    generateClip(gen, context, activeChildrenOutput, output, 0.0f);
                    hkQsTransform worldFromModel;
                    if (gen->m_bReorientSupportChar) {
                        worldFromModel.setInterpolate4(gen->m_StartMarkWS, gen->m_EndMarkWS, hkSimdReal(gen->m_fCurrentLerp));
                    } else {
                        worldFromModel.m_translation = gen->m_EndMarkWS.m_translation;
                        worldFromModel.m_rotation    = gen->m_StartMarkWS.m_rotation;
                        worldFromModel.m_scale       = gen->m_StartMarkWS.m_scale;
                    }
                    hkQsTransform& track = *static_cast<hkQsTransform*>(output.getTrackData(hkbGeneratorOutput::TRACK_WORLD_FROM_MODEL));
                    track.m_rotation     = worldFromModel.m_rotation;
                    track.m_scale        = worldFromModel.m_scale;
                    track.m_translation  = worldFromModel.m_translation;
                    output.getTrackHeader(hkbGeneratorOutput::TRACK_WORLD_FROM_MODEL).m_onFraction = 1.0f;
                    return;
                }
                hkReal clipTimeOffset = 0.0f;
                if (gen->m_bAllCharactersAtMarks) {
                    gen->m_pSyncScene->setTime(timeOffset);
                    clipTimeOffset = gen->m_pSyncScene->getTime();
                }
                generateClip(gen, context, activeChildrenOutput, output, clipTimeOffset);
            } else {
                generateClip(gen, context, activeChildrenOutput, output, 0.0f);
            }
            if (gen->m_bApplyMotionFromRoot && skeleton) applyMotionFromRoot(skeleton, output, gen->m_EndMarkWS);
        }

        /// @note The reference pose is re-read through the character for every bone, as in vanilla.
        void generateClip(BSSynchronizedClipGenerator* gen, const hkbContext& context, const hkbGeneratorOutput** activeChildrenOutput,
                          hkbGeneratorOutput& output, hkReal timeOffset)
        {
            hkbClipGenerator* clip = gen->m_pClipGenerator;
            if (clip && clip->m_userData != 8) {
                clip->generate(context, activeChildrenOutput, output, timeOffset);
                return;
            }
            if (output.isValid(hkbGeneratorOutput::TRACK_POSE)) {
                const int numPoseLocal = output.getTrackHeader(hkbGeneratorOutput::TRACK_POSE).m_numData;
                if (numPoseLocal != 0) {
                    const hkbCharacterSetup* setup = context.m_character->m_setup;
                    if (setup && setup->m_animationSkeleton) {
                        const int numBones = hkMath::min2(numPoseLocal, setup->m_animationSkeleton->m_referencePose.getSize());
                        for (int i = 0; i < numBones; i++) {
                            output.accessPoseLocal()[i] = context.m_character->m_setup->m_animationSkeleton->m_referencePose[i];
                        }
                    }
                }
            }
        }

        void applyMotionFromRoot(const hkaSkeleton* skeleton, hkbGeneratorOutput& output, const hkQsTransform& markWS)
        {
            hkInt16 root = -1;
            for (int i = 0; i < skeleton->m_parentIndices.getSize(); i++) {
                if (root != -1) break;
                if (skeleton->m_parentIndices[i] == -1) root = static_cast<hkInt16>(i);
            }
            if (root != -1) {
                hkQsTransform worldFromModel;
                worldFromModel.setMul(markWS, output.accessPoseLocal()[root]);
                *static_cast<hkQsTransform*>(output.getTrackData(hkbGeneratorOutput::TRACK_WORLD_FROM_MODEL)) = worldFromModel;
                output.getTrackHeader(hkbGeneratorOutput::TRACK_WORLD_FROM_MODEL).m_onFraction = 1.0f;
                output.accessPoseLocal()[root].setIdentity();
            }
        }
    }
} }
