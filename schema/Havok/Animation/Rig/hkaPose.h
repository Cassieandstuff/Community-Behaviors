/// @file
/// @brief hkaPose: a skeleton's pose held in local and model space, each bone synced lazily from the other.
#pragma once

#include <Animation/Rig/hkaSkeleton.h>

namespace CB { namespace schema
{
    /// @note Sized 0x50. Only what reimplemented callers use is declared; the bodies Havok keeps out of line (the
    ///       destructor, the sync and access functions) are vanilla's.
    class hkaPose
    {
    public:
        enum PropagateOrNot { DONT_PROPAGATE = 0, PROPAGATE = 1 };
        /// @brief Per-bone m_boneFlags bits.
        enum BoneFlag { F_BONE_LOCAL_DIRTY = 0x1, F_BONE_MODEL_DIRTY = 0x2, F_BONE_INTERNAL_FLAG1 = 0x4 };

        const hkaSkeleton*             m_skeleton;          ///< +0x00
        mutable hkArray<hkQsTransform> m_localPose;         ///< +0x08
        mutable hkArray<hkQsTransform> m_modelPose;         ///< +0x18
        mutable hkArray<hkUint32>      m_boneFlags;         ///< +0x28, BoneFlag bits
        mutable hkBool                 m_modelInSync;       ///< +0x38
        mutable hkBool                 m_localInSync;       ///< +0x39
        hkArray<hkReal>                m_floatSlotValues;   ///< +0x40

        /// @brief An unset pose sized for `skeleton`: capacities rounded up to a multiple of 4, float slots zeroed.
        HK_FORCE_INLINE hkaPose(const hkaSkeleton* skeleton)
        :   m_skeleton(skeleton), m_modelInSync(false), m_localInSync(false)
        {
            const int numBones          = m_skeleton->m_bones.getSize();
            const int numBonesRoundedUp = (numBones + 3) & ~3;
            m_modelPose.reserveExactly(numBonesRoundedUp);
            m_localPose.reserveExactly(numBonesRoundedUp);
            m_boneFlags.reserveExactly(numBonesRoundedUp);
            m_modelPose.setSize(numBones);
            m_localPose.setSize(numBones);
            m_boneFlags.setSize(numBones);

            const int numFloatSlots = m_skeleton->m_floatSlots.getSize();
            m_floatSlotValues.reserveExactly((numFloatSlots + 3) & ~3);
            m_floatSlotValues.setSize(numFloatSlots, 0.0f);
        }
        /// @brief An unset pose over `preAllocatedMemory` (getRequiredMemorySize bytes): local pose, model pose, bone
        ///        flags and float slots back to back, none of it owned.
        HK_FORCE_INLINE hkaPose(const hkaSkeleton* skeleton, void* preAllocatedMemory)
        :   m_skeleton(skeleton),
            m_localPose(static_cast<hkQsTransform*>(preAllocatedMemory), skeleton->m_bones.getSize(), skeleton->m_bones.getSize()),
            m_modelPose(m_localPose.begin() + skeleton->m_bones.getSize(), skeleton->m_bones.getSize(), skeleton->m_bones.getSize()),
            m_boneFlags(reinterpret_cast<hkUint32*>(m_modelPose.begin() + skeleton->m_bones.getSize()), skeleton->m_bones.getSize(),
                        skeleton->m_bones.getSize()),
            m_modelInSync(false),
            m_localInSync(false),
            m_floatSlotValues(reinterpret_cast<hkReal*>(m_boneFlags.begin() + skeleton->m_bones.getSize()), skeleton->m_floatSlots.getSize(),
                              (skeleton->m_floatSlots.getSize() + 3) & ~3)
        {
        }
        /// @brief The bytes the preallocated constructor needs: both poses and the flags for the bone count rounded up
        ///        to 4, the float slots likewise, the total rounded up to 16.
        static HK_FORCE_INLINE int getRequiredMemorySize(const hkaSkeleton* skeleton)
        {
            const int numBonesRoundedUp      = (skeleton->m_bones.getSize() + 3) & ~3;
            const int numFloatSlotsRoundedUp = (skeleton->m_floatSlots.getSize() + 3) & ~3;
            const int totalSize = numBonesRoundedUp * int(2 * sizeof(hkQsTransform) + sizeof(hkUint32)) + numFloatSlotsRoundedUp * int(sizeof(hkReal));
            return (totalSize + 15) & ~15;
        }
        /// @note Out of line in vanilla (0x140abcbb0).
        ~hkaPose();

        /// @brief The local pose for overwriting: every bone's model transform is marked dirty first.
        hkArray<hkQsTransform>& accessUnsyncedPoseLocalSpace();
        /// @brief The local pose, every dirty bone recomputed from model space first.
        const hkArray<hkQsTransform>& getSyncedPoseLocalSpace() const;
        /// @brief A bone's model transform for writing; with PROPAGATE its descendants keep their model transforms.
        hkQsTransform& accessBoneModelSpace(int boneIdx, PropagateOrNot propagateOrNot = DONT_PROPAGATE);
        /// @brief Recomputes a dirty bone's model transform (up its parent chain) and returns it.
        const hkQsTransform& calculateBoneModelSpace(int boneIdx) const;

        HK_FORCE_INLINE int isFlagOn(int boneIdx, int flag) const { return m_boneFlags[boneIdx] & flag; }
        HK_FORCE_INLINE const hkQsTransform& getBoneModelSpace(int boneIdx) const
        {
            if (!isFlagOn(boneIdx, F_BONE_MODEL_DIRTY)) return m_modelPose[boneIdx];
            return calculateBoneModelSpace(boneIdx);
        }
    };
} }
