/// @file
/// @brief hkbGeneratorTransitionEffectUtils, reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/TransitionEffect/Generator/hkbGeneratorTransitionEffectUtils.h>

#include <Behavior/Generator/hkbGeneratorOutput.h>
#include <Behavior/Generator/hkbGeneratorOutputUtils.h>

namespace CB { namespace schema
{
    namespace hkbGeneratorTransitionEffectUtils
    {
        /// @note A weight of -1 stands for "no blend": the transition generator (the only child) passes through.
        void generateInternal(const hkbGeneratorTransitionEffect& effect, const hkbGeneratorOutput** activeChildrenOutput,
                              hkbGeneratorOutput& output)
        {
            hkReal blendInDuration;
            hkReal blendOutDuration;
            computeBlendDurations(effect, blendInDuration, blendOutDuration);

            hkReal weight = -1.0f;
            switch (effect.m_stage) {
                case hkbGeneratorTransitionEffect::STAGE_BLENDING_IN:
                    weight = (blendInDuration == 0.0f) ? 1.0f : hkMath::min2(effect.m_timeInTransition / blendInDuration, 1.0f);
                    break;
                case hkbGeneratorTransitionEffect::STAGE_PLAYING_TRANSITION_GENERATOR:
                    break;
                default:
                    weight = hkMath::max2((blendOutDuration == 0.0f)
                                              ? 1.0f
                                              : hkMath::max2(effect.m_duration - effect.m_timeInTransition, 0.0f) / blendOutDuration,
                                          0.0f);
                    break;
            }

            if (weight == -1.0f) {
                if (&output != activeChildrenOutput[0]) hkbGeneratorOutputUtils::copyGeneratorOutput(*activeChildrenOutput[0], output);
            } else {
                hkbGeneratorOutputUtils::blendGeneratorOutputs(*activeChildrenOutput[0], *activeChildrenOutput[1], weight, output);
            }
        }

        void computeBlendDurations(const hkbGeneratorTransitionEffect& effect, hkReal& blendInDuration, hkReal& blendOutDuration)
        {
            const hkReal totalDuration = effect.m_effectiveBlendInDuration + effect.m_effectiveBlendOutDuration;
            if (totalDuration > effect.m_duration) {
                blendInDuration  = effect.m_duration / totalDuration * effect.m_effectiveBlendInDuration;
                blendOutDuration = effect.m_duration - blendInDuration;
            } else {
                blendInDuration  = effect.m_effectiveBlendInDuration;
                blendOutDuration = effect.m_effectiveBlendOutDuration;
            }
        }
    }
} }
