/// @file
/// @brief bhkCharacterStateFlying, reimplemented against vanilla (AE 1.6.1170).
#include <Physics/Utilities/CharacterControl/Bethesda/bhkCharacterState.h>

namespace CB { namespace schema
{
    hkpCharacterStateType bhkCharacterStateFlying::getType() const
    {
        return HK_CHARACTER_FLYING;
    }

    /// @note Empty: a flying character's velocity is set by whoever flies it.
    void bhkCharacterStateFlying::Simulate(bhkCharacterController* controller)
    {
    }
} }
