/// @file
/// @brief hkaSkeleton, the mirrored skeleton and the skeleton utilities.
#pragma once

#include <Common/Base/Types/Geometry/LocalFrame/hkLocalFrame.h>

namespace CB { namespace schema
{
    struct hkaBone
    {
        hkStringPtr m_name;              ///< +0x00
        hkBool      m_lockTranslation;   ///< +0x08
    };

    struct hkaSkeleton : hkReferencedObject
    {
        /// @brief A local frame attached to a bone.
        struct LocalFrameOnBone
        {
            hkRefPtr<hkLocalFrame> m_localFrame;   ///< +0x00
            hkInt32                m_boneIndex;    ///< +0x08 — read as 32 bits by its users
        };

        hkStringPtr               m_name;              ///< +0x10
        hkArray<hkInt16>          m_parentIndices;     ///< +0x18 — -1 for a root bone
        hkArray<hkaBone>          m_bones;             ///< +0x28
        hkArray<hkQsTransform>    m_referencePose;     ///< +0x38
        char                      m_pad48[0x68 - 0x48];
        hkArray<LocalFrameOnBone> m_localFrames;       ///< +0x68
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
        /// @brief Bone `boneIndex`'s model-space transform: its local transform composed up the parent chain.
        /// @note Name provisional.
        static void transformLocalBoneToModelBone(int boneIndex, const hkInt16* parentIndices, const hkQsTransform* poseLocal, hkQsTransform& boneModelOut);
    };
} }
