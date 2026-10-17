/// @file
/// @brief hkbStateMachine's internal-state accessors (vtable slots 13, 14, 16), compiled apart from
///        StateMachine\hkbStateMachine.cpp.
/// @note Not a Havok file name. Vanilla keeps these at 0x140b038e0–0x140b0406f, right after hkbClipGenerator's second
///       unit (hkbClipGeneratorSecondUnit.cpp), far from the class's own unit; they may well share that unit.
#include <Behavior/StateMachine/hkbStateMachine.h>

namespace CB { namespace schema
{
    hkReferencedObject* hkbStateMachine::createInternalState()
    {
        return new hkbStateMachineInternalState;
    }

} }
