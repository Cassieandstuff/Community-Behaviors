/// @file
/// @brief hkbBlendCurveUtils: the easing curves blends apply to a linear weight.
#pragma once

#include <Common/Base/hkBase.h>

namespace CB { namespace schema
{
    struct hkbBlendCurveUtils
    {
        enum BlendCurve
        {
            BLEND_CURVE_SMOOTH           = 0,
            BLEND_CURVE_LINEAR           = 1,
            BLEND_CURVE_LINEAR_TO_SMOOTH = 2,
            BLEND_CURVE_SMOOTH_TO_LINEAR = 3
        };

        /// @brief `t` reshaped by `blendCurve` (smoothstep, identity, or a half of each), clamped to [0, 1].
        /// @note Out of line in vanilla (0x140abceb0).
        static hkReal applyBlendCurve(BlendCurve blendCurve, hkReal t);
    };
} }
