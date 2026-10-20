/// @file
/// @brief hkMotionState: a moving body's transform, swept transform and damping.
#pragma once

#include <Common/Base/Math/SweptTransform/hkSweptTransform.h>

namespace CB { namespace havok
{
    /// @brief hkUFloat8's decoding table: the hkReal each of the 256 packed values stands for.
    extern const hkReal hkUFloat8_intToReal[256];

    /// @note 0xb0 bytes (schema/physics/hkMotionState.yaml).
    class hkMotionState
    {
    public:
        hkTransform      m_transform;            ///< +0x00
        hkSweptTransform m_sweptTransform;       ///< +0x40
        hkVector4        m_deltaAngle;           ///< +0x90
        hkReal           m_objectRadius;         ///< +0xa0
        hkHalf           m_linearDamping;        ///< +0xa4
        hkHalf           m_angularDamping;       ///< +0xa6
        hkHalf           m_timeFactor;           ///< +0xa8
        hkUint8          m_maxLinearVelocity;    ///< +0xaa
        hkUint8          m_maxAngularVelocity;   ///< +0xab
        hkUint8          m_deactivationClass;    ///< +0xac

        HK_FORCE_INLINE const hkTransform&      getTransform() const { return m_transform; }
        HK_FORCE_INLINE hkTransform&            getTransform() { return m_transform; }
        HK_FORCE_INLINE const hkSweptTransform& getSweptTransform() const { return m_sweptTransform; }
        /// @brief The packed (hkUFloat8) maximum angular velocity, decoded.
        HK_FORCE_INLINE hkReal getMaxAngularVelocity() const { return hkUFloat8_intToReal[m_maxAngularVelocity]; }
    };
} }
