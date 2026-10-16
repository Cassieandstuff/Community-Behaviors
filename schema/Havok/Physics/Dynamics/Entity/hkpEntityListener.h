/// @file
/// @brief hkpEntityListener: told when an entity is added to, removed from or deleted with a world.
#pragma once

#include <Common/Base/hkBase.h>

namespace CB { namespace schema
{
    /// @note 8 bytes (the vtable). Opaque so far: hkpCharacterProxy derives from it (at +0x10).
    class hkpEntityListener
    {
    public:
        virtual ~hkpEntityListener() {}
    };
} }
