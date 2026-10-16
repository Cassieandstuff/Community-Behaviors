/// @file
/// @brief hkbGeneratorOutputUtils: operations over whole generator outputs.
#pragma once

#include <Behavior/Generator/hkbGeneratorOutput.h>

namespace CB { namespace schema
{
    struct hkaSkeleton;

    /// @note Names provisional.
    struct hkbGeneratorOutputUtils
    {
        /// @brief Copies every track of `in` that is on (onFraction > 0) into `out`, with its header's fraction, data
        ///        size and flags.
        /// @note Vanilla's body is 0x140ad1390, reached through a jump stub (0x140ad26a0) that its callers target.
        static void copyGeneratorOutput(const hkbGeneratorOutput& in, hkbGeneratorOutput& out);
        /// @brief A bone's model-space transform from the output's pose, taking the skeleton's reference pose for
        ///        bones the pose does not set.
        /// @note Out of line in vanilla; shared by hkbSenseHandleModifier and hkbDetectCloseToGroundModifier.
        static void getBoneModelSpace(int boneIndex, const hkbGeneratorOutput& output, const hkaSkeleton* skeleton, hkQsTransform& boneModelSpaceOut);
        /// @brief Fills every pose bone the output's partition bits (after the pose) leave unset from `referencePose`
        ///        (identity when null), then sets all the bits.
        /// @note 0x140ad2a70, formerly pinned as hkbMirrorPose: it makes the pose dense, it does not mirror it.
        static void makePoseDense(hkbGeneratorOutput& output, const hkQsTransform* referencePose);
        /// @brief The output's pose as a full skeleton pose into `poseLocalOut` (the skeleton's bone count): bones the
        ///        partition bits leave unset, and bones past the output's count, come from the reference pose.
        /// @note 0x140abe3e0; the IK modifiers (look-at, foot, hand, limb) load their hkaPose with it.
        static void getFilledPoseLocal(const hkbGeneratorOutput& output, const hkaSkeleton* skeleton, hkQsTransform* poseLocalOut);
        /// @brief The output's pose in model space into `poseModelOut` (the skeleton's bone count), unset bones from the
        ///        reference pose.
        /// @note 0x140abe2a0; hkbGetUpModifier reads the animation pose with it.
        static void getPoseModelSpace(const hkbGeneratorOutput& output, const hkaSkeleton* skeleton, hkQsTransform* poseModelOut);
        /// @brief Sets every partition bit after the output's pose (all bones set), the pose untouched.
        /// @note 0x140abe0e0; the IK modifiers call it after writing their whole pose back.
        static void markPoseDense(hkbGeneratorOutput& output);
        /// @brief `out` = `a` blended toward `b` by `weight` (0 = a, 1 = b): the extracted motion and world-from-model,
        ///        the float tracks, then the pose.
        /// @note Out of line in vanilla (0x140ad2e30); hkbGeneratorTransitionEffect's generate blends its stage's two
        ///       children with it.
        static void blendGeneratorOutputs(const hkbGeneratorOutput& a, const hkbGeneratorOutput& b, hkReal weight, hkbGeneratorOutput& out);
    };
} }
