/// @file
/// @brief hkaSkeletonMapper: maps poses and extracted motion between two skeletons.
#pragma once

#include <Common/Base/hkBase.h>

namespace CB { namespace schema
{
    struct hkaSkeleton;

    /// @note Only the leading members are declared.
    struct hkaSkeletonMapperData
    {
        const hkaSkeleton* m_skeletonA;   ///< +0x00
        const hkaSkeleton* m_skeletonB;   ///< +0x08
    };

    struct hkaSkeletonMapper : hkReferencedObject
    {
        hkaSkeletonMapperData m_mapping;   ///< +0x10

        /// @brief Applies the mapping's extracted-motion transform.
        void mapExtractedMotion(const hkQsTransform& motionIn, hkQsTransform& motionOut) const;
        /// @brief Maps a local-space pose of skeleton A onto `poseBLocalSpaceInOut`, which arrives holding B's base pose.
        ///        With both index tables given, the poses are track-indexed partial poses (bone → track, -1 = absent).
        /// @note Parameter names provisional.
        void mapPoseLocalSpace(const hkQsTransform* poseALocalSpace, hkQsTransform* poseBLocalSpaceInOut, const hkInt16* boneToTrackIndicesA,
                               const hkInt16* boneToTrackIndicesB, bool additive) const;
    };
} }
