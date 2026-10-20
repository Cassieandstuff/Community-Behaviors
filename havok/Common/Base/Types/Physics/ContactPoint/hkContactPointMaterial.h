/// @file
/// @brief hkContactPointMaterial: the per-contact material (friction, restitution, flags) a contact constraint keeps.
#pragma once

#include <Common/Base/hkBase.h>

namespace CB { namespace havok
{
    /// @note 0x10 bytes, per Havok's reflection.
    class hkContactPointMaterial
    {
    public:
        enum FlagEnum
        {
            CONTACT_IS_NEW             = 1,
            CONTACT_USES_SOLVER_PATH2  = 2,
            CONTACT_BREAKOFF_OBJECT_ID = 4,
            CONTACT_IS_DISABLED        = 8,
        };

        hkUlong m_userData;      ///< +0x00
        hkUint8 m_friction;      ///< +0x08
        hkUint8 m_restitution;   ///< +0x09
        hkUint8 m_maxImpulse;    ///< +0x0a
        hkUint8 m_flags;         ///< +0x0b — FlagEnum bits

        HK_FORCE_INLINE bool isDisabled() const { return (m_flags & CONTACT_IS_DISABLED) != 0; }
    };
} }
