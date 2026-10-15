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
    };
} }
