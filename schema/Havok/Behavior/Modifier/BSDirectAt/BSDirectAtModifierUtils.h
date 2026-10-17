/// @file
/// @brief BSDirectAtModifierUtils: BSDirectAtModifier's per-frame work, compiled apart.
#pragma once

#include <Behavior/Modifier/BSDirectAt/BSDirectAtModifier.h>

namespace CB { namespace schema
{
    struct hkaSkeleton;

    struct BSDirectAtModifierUtils
    {
        /// @brief Eases the modifier's heading/pitch offsets toward the target (clamped to the limits, plus the
        ///        offsets; back toward zero without a target), spreads them evenly over the bone chain, and writes the
        ///        turned pose over the output's pose (all bones set).
        /// @note Name provisional (0x140ba1200). Nothing happens without a skeleton or with a chain end outside the
        ///       output's pose. The forward direction is the character's +Y (`worldFromModel`'s rotation); the source
        ///       point is `worldFromModel`'s translation, or m_sourceBoneIndex taken through the OUTPUT's worldFromModel.
        static void modifyInternal(BSDirectAtModifier& modifier, const hkaSkeleton* skeleton, const hkQsTransform& worldFromModel,
                                   const hkVector4& upWS, hkbGeneratorOutput& inOut);

        /// @brief The heading (about Z, from +Y) of `to` minus that of `from`, wrapped into [-pi, pi].
        /// @note Name provisional (0x140ba1f00).
        static hkReal computeHeadingDelta(const hkVector4& from, const hkVector4& to);
        /// @brief The pitch (elevation above the XY plane) of `to` minus that of `from`, wrapped into [-pi, pi].
        /// @note Name provisional (0x140ba1f70). Both elevations divide by `to`'s horizontal length (vanilla's, kept).
        static hkReal computePitchDelta(const hkVector4& from, const hkVector4& to);
    };
} }
