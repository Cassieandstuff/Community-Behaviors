/// @file
/// @brief BSLookAtModifierUtils: BSLookAtModifier's per-frame work, compiled apart (Modifier\BSLookAt\BSLookAtModifierUtils.cpp).
#pragma once

#include <Behavior/Modifier/BSLookAt/BSLookAtModifier.h>

namespace CB { namespace schema
{
    class hkaPose;

    struct BSLookAtModifierUtils
    {
        /// @brief Picks the target (m_targetLocation, or the camera point), decides whether it is outside the limit cone
        ///        (raising m_targetOutOfLimitEvent once on leaving), turns every bone and eye bone toward it, then writes
        ///        `pose` back over the output's pose (all bones set).
        /// @note `context` may be null: the out-of-limits event is then dropped with a warning.
        static void modifyInternal(BSLookAtModifier& modifier, hkaPose& pose, hkbGeneratorOutput& inOut, const hkbContext* context);
    };
} }
