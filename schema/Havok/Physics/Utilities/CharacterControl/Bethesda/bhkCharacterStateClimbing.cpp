/// @file
/// @brief bhkCharacterStateClimbing, reimplemented against vanilla (AE 1.6.1170).
#include <Physics/Utilities/CharacterControl/Bethesda/bhkCharacterState.h>
#include <Physics/Utilities/CharacterControl/Bethesda/bhkCharacterController.h>

namespace CB { namespace schema
{
    hkpCharacterStateType bhkCharacterStateClimbing::getType() const
    {
        return HK_CHARACTER_CLIMBING;
    }

    /// @note Any request leaves at once; otherwise the character moves straight up (or down) at the movement asked
    ///       along its forward axis (m_velocityMod's y), and may jump (kCanJump).
    void bhkCharacterStateClimbing::Simulate(bhkCharacterController* controller)
    {
        if (controller->m_wantState != HK_CHARACTER_MAX_STATE_ID) {
            controller->CommitWantedState();
            return;
        }
        controller->m_flags |= 0x400;
        controller->m_outVelocity.set(0.0f, 0.0f, controller->m_velocityMod(1), 0.0f);
    }
} }
