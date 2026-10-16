/// @file
/// @brief hkbCharacterControllerModifier, reimplemented against vanilla (AE 1.6.1170) — its internal state's destructor
///        so far.
#include <Behavior/Modifier/CharacterController/hkbCharacterControllerModifier.h>

namespace CB { namespace schema
{
    hkbCharacterControllerModifierInternalState::hkbCharacterControllerModifierInternalState(hkFinishLoadedObjectFlag flag)
    :   hkReferencedObject(flag)
    {
    }
} }
