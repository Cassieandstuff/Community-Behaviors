/// @file
/// @brief hkbMoveCharacterModifierUtils: the world-from-model step behind hkbMoveCharacterModifier::modify.
/// @note A file of its own in vanilla: the function sits at 0x140b0df70, far from the class's own functions, in the run
///       of out-of-line helpers of a dozen node classes (0x140b0c450–0x140b0fb0f) — Havok's `…Utils` split, as the exe's
///       Modifier\BSLookAt\BSLookAtModifierUtils.cpp is for BSLookAtModifier. The file name is inferred.
#pragma once

#include <Behavior/Modifier/MoveCharacter/hkbMoveCharacterModifier.h>

namespace CB { namespace havok
{
    namespace hkbMoveCharacterModifierUtils
    {
        /// @brief Moves the output's world-from-model by m_offsetPerSecondMS · m_timeSinceLastModify, rotated into world
        ///        space by its rotation, then clears m_timeSinceLastModify; nothing while no time is pending.
        /// @note Name provisional.
        void modify(hkbMoveCharacterModifier& modifier, hkbGeneratorOutput& inOut);
    }
} }
