/// @file
/// @brief hkpSurfaceInfo: what a character controller's support query found under it.
#pragma once

#include <Common/Base/hkBase.h>

namespace CB { namespace schema
{
    /// @note 0x40 bytes.
    struct hkpSurfaceInfo
    {
        /// @note Values per the Havok SDK; SUPPORTED (2) is confirmed by hkbProxyModifier.
        enum SupportedState
        {
            UNSUPPORTED = 0,
            SLIDING     = 1,
            SUPPORTED   = 2,
        };

        /// @note Storage width unconfirmed (vanilla writes and tests it as a dword).
        hkEnum<SupportedState, hkUint32> m_supportedState;          ///< +0x00
        hkVector4                        m_surfaceNormal;           ///< +0x10
        hkVector4                        m_surfaceVelocity;         ///< +0x20
        hkReal                           m_surfaceDistanceExcess;   ///< +0x30
        hkBool                           m_surfaceIsDynamic;        ///< +0x34
    };
} }
