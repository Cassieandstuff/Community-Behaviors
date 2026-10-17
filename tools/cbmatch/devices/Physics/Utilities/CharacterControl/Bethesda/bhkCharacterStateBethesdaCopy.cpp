/// @file
/// @brief The character states' COMDAT copies the exe keeps in bhkCharacterController's translation unit
///        (0x140ea3ca0..0x140ea4e40): the scalar deleting destructors of hkpCharacterState, bhkCharacterState and the
///        climbing and flying states, and bhkCharacterState's empty update / change. A matching device rather than a
///        vanilla file: vanilla instantiates the climbing and flying states inline in bhkCharacterControllerCinfo's
///        constructor (0x140e9c960).
/// @note Bethesda's code is built with exceptions on and /Ob2 (set on this unit).
#include <Physics/Utilities/CharacterControl/Bethesda/bhkCharacterState.h>

namespace CB { namespace schema
{
    /// @brief Instantiates the climbing and flying states' vtables (and their bases'), which brings the copies into
    ///        this unit.
    void bhkCharacterStateBethesdaCopy_instantiate(hkpCharacterState** climbing, hkpCharacterState** flying)
    {
        *climbing = new bhkCharacterStateClimbing;
        *flying   = new bhkCharacterStateFlying;
    }
} }
