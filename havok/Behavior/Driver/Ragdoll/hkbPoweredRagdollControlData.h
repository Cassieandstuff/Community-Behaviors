/// @file
/// @brief hkbPoweredRagdollControlData: the motor settings the powered ragdoll controller drives the ragdoll with.
/// @note The folder is a guess (no __FILE__ string): beside hkbRagdollDriver, which reads them off the output's track.
#pragma once

#include <Common/Base/hkBase.h>

namespace CB { namespace havok
{
    /// @note 0x20 bytes, 16-aligned (reflected size 32 over 20 bytes of members). The alignment sits on the first member
    ///       (Havok's HK_ALIGN16 style): aligning the struct instead turns the clone copy's movaps loads into movups.
    struct hkbPoweredRagdollControlData
    {
        __declspec(align(16)) hkReal m_maxForce;                       ///< +0x00
        hkReal m_tau;                            ///< +0x04
        hkReal m_damping;                        ///< +0x08
        hkReal m_proportionalRecoveryVelocity;   ///< +0x0c
        hkReal m_constantRecoveryVelocity;       ///< +0x10

        /// @note The defaults vanilla's hkbPoweredRagdollControlsModifier constructor stores.
        HK_FORCE_INLINE hkbPoweredRagdollControlData()
        :   m_maxForce(50.0f), m_tau(0.8f), m_damping(1.0f), m_proportionalRecoveryVelocity(2.0f), m_constantRecoveryVelocity(1.0f) {}
    };
    static_assert(sizeof(hkbPoweredRagdollControlData) == 0x20, "hkbPoweredRagdollControlData size");
} }
