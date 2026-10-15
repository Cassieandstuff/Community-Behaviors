/// @file
/// @brief hkbCharacterControllerDriver: only the slot the reimplemented code calls.
#pragma once

#include <Common/Base/hkBase.h>

namespace CB { namespace schema
{
    /// @note Slots 3–9 are placeholders.
    class hkbCharacterControllerDriver : public hkReferencedObject
    {
    public:
        virtual void vfn3();
        virtual void vfn4();
        virtual void vfn5();
        virtual void vfn6();
        virtual void vfn7();
        virtual void vfn8();
        virtual void vfn9();
        /// @note Name provisional: hkbCharacter::deactivate calls it beside the other drivers' resets.
        virtual void reset();   ///< slot 10
    };
} }
