/// @file
/// @brief hkaSkeleton, the mirrored skeleton and the skeleton utilities.
#pragma once

#include <Common/Base/hkBase.h>

namespace CB { namespace schema
{
    struct hkaSkeleton : hkReferencedObject
    {
        hkStringPtr            m_name;            ///< +0x10
        char                   m_pad18[0x38 - 0x18];
        hkArray<hkQsTransform> m_referencePose;   ///< +0x38
    };

    struct hkaMirroredSkeleton : hkReferencedObject
    {
        void mirrorExtractedMotion(hkQsTransform& motion) const;
    };

    struct hkaSkeletonUtils
    {
        /// @note Name provisional; (scale, pose, count).
        static void scalePose(hkReal scale, hkQsTransform* poseInOut, int numTransforms);
        /// @brief The highest track below `numTracks` that drives a bone in [0, numBones) and is enabled, else -1; with
        ///        no `trackToBone` table, track i drives bone i.
        /// @note Name provisional; `tracksEnabled` may be null (every track enabled).
        static int findLastUsedTrack(const hkInt16* trackToBone, const hkBool* tracksEnabled, int numBones, int numTracks);
    };
} }
