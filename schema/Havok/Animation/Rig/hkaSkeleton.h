/// @file
/// @brief hkaSkeleton, the mirrored skeleton and the skeleton utilities.
#pragma once

#include <Common/Base/hkBase.h>

namespace CB { namespace schema
{
    struct hkaSkeleton : hkReferencedObject
    {
        hkStringPtr            m_name;            ///< +0x10
        hkArray<hkInt16>       m_parentIndices;   ///< +0x18 — -1 for a root bone
        char                   m_pad28[0x38 - 0x28];
        hkArray<hkQsTransform> m_referencePose;   ///< +0x38
    };

    struct hkaMirroredSkeleton : hkReferencedObject
    {
        void mirrorExtractedMotion(hkQsTransform& motion) const;
        /// @brief The bone's mirror partner; -1 for a negative index.
        hkInt16 getBonePair(hkInt16 bone) const;
        /// @brief Mirrors one bone's transform in place.
        /// @note Name provisional.
        void mirrorBone(hkQsTransform& transformInOut, hkInt16 bone, bool isAdditive) const;
    };

    struct hkaSkeletonUtils
    {
        /// @note Name provisional; (scale, pose, count).
        static void scalePose(hkReal scale, hkQsTransform* poseInOut, int numTransforms);
        /// @brief The highest track below `numTracks` that drives a bone in [0, numBones) and is enabled, else -1; with
        ///        no `trackToBone` table, track i drives bone i.
        /// @note Name provisional; `tracksEnabled` may be null (every track enabled).
        static int findLastUsedTrack(const hkInt16* trackToBone, const hkBool* tracksEnabled, int numBones, int numTracks);
        /// @brief The chain from `startBone` down to `endBone`, start first, into `bonesOut` (resized to fit); false, with
        ///        `bonesOut` untouched, when the walk up from `endBone` reaches a negative parent.
        static hkBool getBoneChain(const hkaSkeleton& skeleton, hkInt16 startBone, hkInt16 endBone, hkArray<hkInt16>& bonesOut);
    };
} }
