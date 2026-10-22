/// @file
/// @brief hkpPhantomListener: told when a phantom is added to, removed from or deleted with a world.
#pragma once

#include <Common/Base/hkBase.h>

namespace CB { namespace havok
{
    /// @note 8 bytes (the vtable). Opaque so far: hkpCharacterProxy derives from it (at +0x18).
    class hkpPhantomListener
    {
    public:
        virtual ~hkpPhantomListener() {}
    };
} }
