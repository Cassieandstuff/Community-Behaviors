/// @file
/// @brief hkpCharacterMovementUtil: steers a character's velocity toward a desired one in the surface's frame.
#pragma once

#include <Common/Base/hkBase.h>

namespace CB { namespace schema
{
    /// @brief What calculateMovement steers by.
    /// @note 0x70 bytes. Not reflected; the fields per the Havok SDK, their offsets proven by Bethesda's states, which
    ///       fill every one.
    struct hkpMovementUtilInput
    {
        hkVector4 m_forward;            ///< +0x00
        hkVector4 m_up;                 ///< +0x10
        hkVector4 m_surfaceNormal;      ///< +0x20
        hkVector4 m_currentVelocity;    ///< +0x30
        /// @brief In the character's frame (x along m_forward, z along m_up), per the Havok SDK.
        hkVector4 m_desiredVelocity;    ///< +0x40
        hkVector4 m_surfaceVelocity;    ///< +0x50
        hkReal    m_gain;               ///< +0x60
        hkReal    m_maxVelocityDelta;   ///< +0x64
    };

    class hkpCharacterMovementUtil
    {
    public:
        /// @note Out of line in vanilla (0x140f3d670).
        static void calculateMovement(const hkpMovementUtilInput& input, hkVector4& velocityOut);
    };
} }
