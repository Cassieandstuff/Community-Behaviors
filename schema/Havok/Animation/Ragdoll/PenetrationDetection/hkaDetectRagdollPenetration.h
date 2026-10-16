/// @file
/// @brief hkaDetectRagdollPenetration: finds the ragdoll bone segments that pass through the world.
/// @note The folder is a guess (the exe keeps no __FILE__ string for it).
#pragma once

#include <Animation/Ragdoll/PenetrationDetection/hkaRagdollRaycastInterface.h>
#include <Animation/Rig/hkaSkeleton.h>

namespace CB { namespace schema
{
    class hkaPose;

    /// @note 0x48 bytes. Member names provisional except m_setup's fields.
    class hkaDetectRagdollPenetration : public hkReferencedObject
    {
    public:
        /// @brief A bone's state between calls.
        /// @note Names and values provisional: every bone starts at 1.
        enum BonePenetrationStatus
        {
            HK_NOP  = 0,
            HK_YESP = 1,
        };

        /// @note 0x18 bytes.
        struct Setup
        {
            const hkaSkeleton*          m_ragdollSkeleton;    ///< +0x00
            hkaRagdollRaycastInterface* m_raycastInterface;   ///< +0x08
            hkpAabbPhantom*             m_ragdollPhantom;     ///< +0x10
        };

        /// @brief A penetrating bone segment: its begin and end bones, the hit point and normal.
        /// @note 0x30 bytes.
        struct BonePenetration
        {
            hkInt16   m_boneBeginIndex;      ///< +0x00
            hkInt16   m_boneEndIndex;        ///< +0x02
            hkVector4 m_penetrationPoint;    ///< +0x10
            hkVector4 m_penetrationNormal;   ///< +0x20
        };

        Setup                          m_setup;          ///< +0x10
        int                            m_numBones;       ///< +0x28
        hkArray<BonePenetrationStatus> m_boneStates;     ///< +0x30
        hkBool                         m_ragdollIsOut;   ///< +0x40

        hkaDetectRagdollPenetration(const Setup& setup);

        /// @brief Casts each parent → child segment of `ragdollPoseIn` (within the phantom's AABB) and reports the
        ///        penetrating ones.
        /// @note Out of line in vanilla (0x140eff8a0), not reimplemented yet; the hkBool comes back through memory.
        hkBool detectPenetration(const hkaPose& ragdollPoseIn, hkArray<BonePenetration>& penetrationsOut);
    };
} }
