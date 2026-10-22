/// @file
/// @brief hkpSurfaceInfo: what a character controller's support query found under it; hkpCharacterControllerCinfo, the
///        base of a character controller's construction info.
#pragma once

#include <Common/Base/hkBase.h>

namespace CB { namespace havok
{
    /// @brief Seeded from Havok's reflection (size 0x10): no members of its own; the proxy and rigid-body controllers'
    ///        construction info derive from it.
    class hkpCharacterControllerCinfo : public hkReferencedObject
    {
    };

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

        /// @note Havok's defaults (supported, on a z-up floor), as hkbProxyModifier::modify's inlined copy stores them.
        HK_FORCE_INLINE hkpSurfaceInfo()
        :   m_supportedState(SUPPORTED),
            m_surfaceDistanceExcess(0.0f),
            m_surfaceIsDynamic(false)
        {
            m_surfaceNormal.set(0.0f, 0.0f, 1.0f);
            m_surfaceVelocity.setZero4();
        }
    };
} }
