/// @file
/// @brief hkaRaycastInterface's scalar deleting destructor as the exe keeps it: a COMDAT copy from a Bethesda
///        translation unit (0x1405445f0, beside the game's foot IK raycast classes), a matching device rather than a
///        vanilla file.
/// @note Bethesda's code is built with exceptions on (/EHsc, set on this unit): the copy carries the EH frame state
///       (`mov [rsp+0x20], -2`) and the call-site nops.
#include <Animation/Ik/FootPlacement/hkaRaycastInterface.h>

namespace CB { namespace havok
{
    /// @brief Instantiates the vtable, which brings the deleting destructor into this unit.
    hkaRaycastInterface* hkaRaycastInterfaceBethesdaCopy_instantiate()
    {
        return new hkaRaycastInterface;
    }
} }
