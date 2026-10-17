/// @file
/// @brief hkbTwistModifierUtils: the pose work behind hkbTwistModifier::modify.
/// @note A file of its own in vanilla: the function sits at 0x140b0f3e0, far from the class's own functions, in the run
///       of out-of-line helpers of a dozen node classes (0x140b0c450–0x140b0fb0f) — Havok's `…Utils` split, as the exe's
///       Modifier\BSLookAt\BSLookAtModifierUtils.cpp is for BSLookAtModifier. The file name is inferred.
#pragma once

#include <Behavior/Modifier/Twist/hkbTwistModifier.h>

namespace CB { namespace schema
{
    struct hkbGeneratorOutput;

    namespace hkbTwistModifierUtils
    {
        /// @brief Twists the bone chain in `inOut`'s pose (filled in from `referencePose` first) by m_twistAngle about
        ///        m_axisOfRotation, spread over the chain (evenly, or ramped).
        /// @note Name provisional, after Bethesda's own `BSLookAtModifierUtils::modifyInternal` (the exe's strings).
        void modifyInternal(const hkbTwistModifier& modifier, hkbGeneratorOutput& inOut, const hkQsTransform* referencePose);
    }
} }
