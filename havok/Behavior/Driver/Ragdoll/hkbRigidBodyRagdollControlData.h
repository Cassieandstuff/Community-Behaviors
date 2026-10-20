/// @file
/// @brief hkbRigidBodyRagdollControlData: the keyframe-hierarchy gains the rigid body ragdoll controller drives the
///        ragdoll with, and how long to blend into it.
/// @note The folder is a guess (no __FILE__ string): beside hkbRagdollDriver, which reads them off the output's track.
#pragma once

#include <Animation/Ragdoll/Controller/RigidBody/hkaKeyFrameHierarchyUtility.h>

namespace CB { namespace havok
{
    /// @note 0x40 bytes, 16-aligned (reflected size 64 over 52 bytes of members).
    struct __declspec(align(16)) hkbRigidBodyRagdollControlData
    {
        hkaKeyFrameHierarchyUtility::ControlData m_keyFrameHierarchyControlData;   ///< +0x00
        hkReal                                   m_durationToBlend;                ///< +0x30

        HK_FORCE_INLINE hkbRigidBodyRagdollControlData() : m_durationToBlend(0.0f) {}
    };
    static_assert(sizeof(hkbRigidBodyRagdollControlData) == 0x40, "hkbRigidBodyRagdollControlData size");
} }
