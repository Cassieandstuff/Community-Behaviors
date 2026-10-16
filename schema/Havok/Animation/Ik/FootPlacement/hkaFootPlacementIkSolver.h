/// @file
/// @brief hkaFootPlacementIkSolver: places one leg's foot on the ground (one per leg of hkbFootIkDriver).
#pragma once

#include <Common/Base/hkBase.h>

namespace CB { namespace schema
{
    /// @note Opaque so far: hkbFootIkDriver only deletes it (through the virtual destructor).
    class hkaFootPlacementIkSolver : public hkReferencedObject
    {
    };
} }
