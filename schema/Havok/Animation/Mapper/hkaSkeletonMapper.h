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
        /// @brief Where mapPose takes the constraints for B's unmapped bones from.
        /// @note Values per the Havok SDK.
        enum ConstraintSource { NO_CONSTRAINTS = 0, REFERENCE_POSE = 1, CURRENT_POSE = 2 };

        hkaSkeletonMapperData m_mapping;   ///< +0x10

        /// @brief Applies the mapping's extracted-motion transform.
        void mapExtractedMotion(const hkQsTransform& motionIn, hkQsTransform& motionOut) const;
        /// @brief Maps a local-space pose of skeleton A onto `poseBLocalSpaceInOut`, which arrives holding B's base pose.
        ///        With both index tables given, the poses are track-indexed partial poses (bone → track, -1 = absent).
        /// @note Parameter names provisional.
        void mapPoseLocalSpace(const hkQsTransform* poseALocalSpace, hkQsTransform* poseBLocalSpaceInOut, const hkInt16* boneToTrackIndicesA,
                               const hkInt16* boneToTrackIndicesB, bool additive) const;
        /// @brief Maps a model-space pose of skeleton A onto `poseBModelSpaceInOut`, B's unmapped bones following
        ///        `originalPoseBLocalSpace` as `source` says.
        /// @note Parameter names per the Havok SDK; out of line in vanilla (0x140bd0000).
        void mapPose(const hkQsTransform* poseAModelSpace, const hkQsTransform* originalPoseBLocalSpace, hkQsTransform* poseBModelSpaceInOut,
                     ConstraintSource source) const;
    };
} }
