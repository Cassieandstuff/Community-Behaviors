/// @file
/// @brief hkbGeneratorTransitionEffectUtils: the pose work behind hkbGeneratorTransitionEffect::generate.
/// @note A file of its own in vanilla: both functions sit at 0x140b0fa30-0x140b0fb65, at the end of the run of node
///       classes' out-of-line helpers, right after hkbBlendingTransitionEffectUtils (a Havok name, from the exe's monitor
///       string "hkbBlendingTransitionEffectUtils::generateInternal"). This file name is inferred by analogy.
#pragma once

#include <Behavior/TransitionEffect/Generator/hkbGeneratorTransitionEffect.h>

namespace CB { namespace schema
{
    struct hkbGeneratorOutput;

    namespace hkbGeneratorTransitionEffectUtils
    {
        /// @brief Blends the stage's two children (from- into transition generator, or transition into to-generator) by
        ///        the time spent in the blend, or passes the transition generator's output through.
        /// @note Name provisional, after hkbBlendingTransitionEffectUtils::generateInternal.
        void generateInternal(const hkbGeneratorTransitionEffect& effect, const hkbGeneratorOutput** activeChildrenOutput,
                              hkbGeneratorOutput& output);
        /// @brief The effective blend-in and blend-out durations, scaled down to fit m_duration when they overlap.
        /// @note Name provisional.
        void computeBlendDurations(const hkbGeneratorTransitionEffect& effect, hkReal& blendInDuration, hkReal& blendOutDuration);
    }
} }
