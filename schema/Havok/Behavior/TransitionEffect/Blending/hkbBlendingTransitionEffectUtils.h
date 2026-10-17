/// @file
/// @brief hkbBlendingTransitionEffectUtils: the pose work behind hkbBlendingTransitionEffect::generate.
/// @note Havok's name (the exe's monitor string "hkbBlendingTransitionEffectUtils::generateInternal"); vanilla's copy
///       sits at 0x140b0f830, in the run of node classes' out-of-line helpers.
#pragma once

#include <Behavior/TransitionEffect/Blending/hkbBlendingTransitionEffect.h>

namespace CB { namespace schema
{
    struct hkbGeneratorOutput;

    namespace hkbBlendingTransitionEffectUtils
    {
        /// @brief Cross-fades the from-generator's output (activeChildrenOutput[1]) into the to-generator's ([0]) by the
        ///        transition's progress at `timeOffset`, shaped by m_blendCurve; a self transition passes [0] through.
        /// @note Hands a powered ragdoll over to a rigid-body one: when only the from-output drives the powered ragdoll
        ///       and the to-output the rigid-body ragdoll, it switches the from-output's powered controls off, overwrites
        ///       its pose with the character's pose at the start of the transition (`poseLocal` until that is taken)
        ///       and raises m_initializeCharacterPose. The from-child's output buffer is written.
        void generateInternal(hkbBlendingTransitionEffect& effect, hkReal timeOffset, const hkQsTransform* poseLocal,
                              const hkbGeneratorOutput** activeChildrenOutput, hkbGeneratorOutput& output);
    }
} }
