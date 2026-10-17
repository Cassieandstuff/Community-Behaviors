/// @file
/// @brief hkaKeyFrameHierarchyUtility: drives a ragdoll's rigid bodies toward a keyframed pose.
/// @note Only the control data so far (hkbRigidBodyRagdollControlData embeds it); the utility's functions stay in the exe.
#pragma once

#include <Common/Base/hkBase.h>

namespace CB { namespace schema
{
    class hkaKeyFrameHierarchyUtility
    {
    public:
        /// @brief The gains of one bone's controller (reflected as hkaKeyFrameHierarchyUtilityControlData, 0x30 bytes).
        /// @note 16-aligned: the hkb control data that embeds it copies it with movaps.
        struct __declspec(align(16)) ControlData
        {
            hkReal m_hierarchyGain;                ///< +0x00
            hkReal m_velocityDamping;              ///< +0x04
            hkReal m_accelerationGain;             ///< +0x08
            hkReal m_velocityGain;                 ///< +0x0c
            hkReal m_positionGain;                 ///< +0x10
            hkReal m_positionMaxLinearVelocity;    ///< +0x14
            hkReal m_positionMaxAngularVelocity;   ///< +0x18
            hkReal m_snapGain;                     ///< +0x1c
            hkReal m_snapMaxLinearVelocity;        ///< +0x20
            hkReal m_snapMaxAngularVelocity;       ///< +0x24
            hkReal m_snapMaxLinearDistance;        ///< +0x28
            hkReal m_snapMaxAngularDistance;       ///< +0x2c

            /// @note The defaults vanilla's hkbRigidBodyRagdollControlsModifier constructor stores.
            HK_FORCE_INLINE ControlData()
            :   m_hierarchyGain(0.17f),
                m_velocityDamping(0.0f),
                m_accelerationGain(1.0f),
                m_velocityGain(0.6f),
                m_positionGain(0.05f),
                m_positionMaxLinearVelocity(1.4f),
                m_positionMaxAngularVelocity(1.8f),
                m_snapGain(0.1f),
                m_snapMaxLinearVelocity(0.3f),
                m_snapMaxAngularVelocity(0.3f),
                m_snapMaxLinearDistance(0.03f),
                m_snapMaxAngularDistance(0.1f)
            {
            }
        };
    };
    static_assert(sizeof(hkaKeyFrameHierarchyUtility::ControlData) == 0x30, "hkaKeyFrameHierarchyUtility::ControlData size");
} }
