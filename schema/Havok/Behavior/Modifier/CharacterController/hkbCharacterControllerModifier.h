/// @file
/// @brief hkbCharacterControllerModifier (drives the physics character controller): only its internal state is declared
///        so far.
#pragma once

#include <Common/Base/hkBase.h>

namespace CB { namespace schema
{
    /// @note 0x30 bytes; member names after the Havok SDK, unverified beyond the size.
    struct hkbCharacterControllerModifierInternalState : hkReferencedObject
    {
        hkVector4 m_gravity;                  ///< +0x10
        hkReal    m_timestep;                 ///< +0x20
        hkBool    m_isInitialVelocityAdded;   ///< +0x24
        hkBool    m_isTouchingGround;         ///< +0x25

        HK_FORCE_INLINE hkbCharacterControllerModifierInternalState() {}
        /// @note Vanilla inlines it into the type registry's finish function; out of line here, it also brings the
        ///       class's vtable and scalar deleting destructor into hkbCharacterControllerModifier.cpp.
        hkbCharacterControllerModifierInternalState(hkFinishLoadedObjectFlag flag);
    };
} }
