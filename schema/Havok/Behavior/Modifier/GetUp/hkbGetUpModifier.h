/// @file
/// @brief hkbGetUpModifier (a ragdoll get-up blend): only its internal state is declared so far.
#pragma once

#include <Common/Base/hkBase.h>

namespace CB { namespace schema
{
    /// @note 0x20 bytes; member names after the Havok SDK, unverified beyond the size.
    struct hkbGetUpModifierInternalState : hkReferencedObject
    {
        hkReal m_timeSinceBegin;   ///< +0x10
        hkReal m_timeStep;         ///< +0x14
        hkBool m_initNextModify;   ///< +0x18

        HK_FORCE_INLINE hkbGetUpModifierInternalState() {}
        /// @note Vanilla inlines it into the type registry's finish function; out of line here, it also brings the
        ///       class's vtable and scalar deleting destructor into hkbGetUpModifier.cpp.
        hkbGetUpModifierInternalState(hkFinishLoadedObjectFlag flag);
    };
} }
