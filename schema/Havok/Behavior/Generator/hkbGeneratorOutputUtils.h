/// @file
/// @brief hkbGeneratorOutputUtils: operations over whole generator outputs.
#pragma once

#include <Behavior/Generator/hkbGeneratorOutput.h>

namespace CB { namespace schema
{
    struct hkbGeneratorOutputUtils
    {
        /// @brief Copies every track of `in` that is on (onFraction > 0) into `out`, with its header's fraction, data
        ///        size and flags.
        /// @note Name provisional. Vanilla's body is 0x140ad1390, reached through a jump stub (0x140ad26a0) that its
        ///       callers target.
        static void copyGeneratorOutput(const hkbGeneratorOutput& in, hkbGeneratorOutput& out);
    };
} }
