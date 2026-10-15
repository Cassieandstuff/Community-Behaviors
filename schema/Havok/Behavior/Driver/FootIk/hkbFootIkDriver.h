/// @file
/// @brief hkbFootIkDriver: only what the reimplemented code calls.
#pragma once

#include <Common/Base/hkBase.h>

namespace CB { namespace schema
{
    class hkbFootIkDriver : public hkReferencedObject
    {
    public:
        /// @brief Drops every leg's solver and the ground state (hkbCharacter::deactivate).
        /// @note Out of line in vanilla (0x140ab9df0).
        void reset();
    };
} }
