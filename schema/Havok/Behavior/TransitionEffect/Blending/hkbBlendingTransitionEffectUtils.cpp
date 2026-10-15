/// @file
/// @brief hkbBlendingTransitionEffectUtils, reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/TransitionEffect/Blending/hkbBlendingTransitionEffectUtils.h>

#include <Behavior/Generator/hkbGeneratorOutput.h>
#include <Behavior/Generator/hkbGeneratorOutputUtils.h>
#include <Behavior/Utils/hkbBlendCurveUtils.h>
#include <Common/Base/Monitor/hkMonitorStream.h>

namespace CB { namespace schema
{
    namespace hkbBlendingTransitionEffectUtils
    {
        void generateInternal(hkbBlendingTransitionEffect& effect, hkReal timeOffset, const hkQsTransform* poseLocal,
                              const hkbGeneratorOutput** activeChildrenOutput, hkbGeneratorOutput& output)
        {
            HK_TIMER_BEGIN("hkbBlendingTransitionEffectUtils::generateInternal");

            if (effect.m_fromGenerator == effect.m_toGenerator) {
                if (&output != activeChildrenOutput[0]) hkbGeneratorOutputUtils::copyGeneratorOutput(*activeChildrenOutput[0], output);
            } else {
                hkbGeneratorOutput& fromOutput = const_cast<hkbGeneratorOutput&>(*activeChildrenOutput[1]);
                hkbGeneratorOutput& toOutput   = const_cast<hkbGeneratorOutput&>(*activeChildrenOutput[0]);

                if (fromOutput.isTrackOn(hkbGeneratorOutput::TRACK_POWERED_RAGDOLL_CONTROLS) &&
                    !fromOutput.isTrackOn(hkbGeneratorOutput::TRACK_RIGID_BODY_RAGDOLL_CONTROLS) &&
                    toOutput.isTrackOn(hkbGeneratorOutput::TRACK_RIGID_BODY_RAGDOLL_CONTROLS))
                {
                    fromOutput.getTrackHeader(hkbGeneratorOutput::TRACK_POWERED_RAGDOLL_CONTROLS).m_onFraction = 0.0f;
                    const int numPoseLocal = fromOutput.getNumPoseLocal();
                    if (effect.m_characterPoseAtBeginningOfTransition.getSize() != 0) poseLocal = effect.m_characterPoseAtBeginningOfTransition.begin();
                    hkString::memCpy(fromOutput.accessPoseLocal(), poseLocal, numPoseLocal * int(sizeof(hkQsTransform)));
                    effect.m_initializeCharacterPose = true;
                }

                const hkReal totalTime = effect.m_timeInTransition + effect.m_timeRemaining;
                hkReal       weight    = 0.0f;
                if (totalTime != 0.0f) weight = hkMath::min2(effect.m_timeInTransition + timeOffset, totalTime) / totalTime;
                if (weight >= 1.0f) weight = 1.0f;
                const hkReal blendWeight =
                    hkbBlendCurveUtils::applyBlendCurve(static_cast<hkbBlendCurveUtils::BlendCurve>(effect.m_blendCurve), weight);
                hkbGeneratorOutputUtils::blendGeneratorOutputs(fromOutput, toOutput, blendWeight, output);

                if (effect.m_flags & hkbBlendingTransitionEffect::FLAG_IGNORE_FROM_WORLD_FROM_MODEL) {
                    output.accessWorldFromModel() = toOutput.accessWorldFromModel();
                } else if (effect.m_flags & hkbBlendingTransitionEffect::FLAG_IGNORE_TO_WORLD_FROM_MODEL) {
                    output.accessWorldFromModel() = fromOutput.accessWorldFromModel();
                }
            }

            HK_TIMER_END();
        }
    }
} }
