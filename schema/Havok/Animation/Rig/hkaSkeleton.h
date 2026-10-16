/// @file
/// @brief hkaSkeleton, the mirrored skeleton and the skeleton utilities.
#pragma once

#include <Common/Base/Container/StringMap/hkStringMap.h>
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
        hkArray<hkReal>           m_referenceFloats;   ///< +0x48
        hkArray<hkStringPtr>      m_floatSlots;        ///< +0x58
        hkArray<LocalFrameOnBone> m_localFrames;       ///< +0x68

        HK_FORCE_INLINE hkaSkeleton() {}
        HK_FORCE_INLINE hkaSkeleton(hkFinishLoadedObjectFlag flag)
        :   hkReferencedObject(flag),
            m_name(flag),
            m_parentIndices(flag),
            m_bones(flag),
            m_referencePose(flag),
            m_referenceFloats(flag),
            m_floatSlots(flag),
            m_localFrames(flag)
        {
        }
    };

    struct hkaMirroredSkeleton : hkReferencedObject
    {
        /// @note Member names after m_bonePairMap provisional; the two tables are hkAlignedAllocate blocks it owns.
        hkInt16*             m_bonePairMap;      ///< +0x10, bone → mirror partner
        const hkaSkeleton*   m_skeleton;         ///< +0x18
        const hkInt16*       m_parentIndices;    ///< +0x20, the skeleton's
        int                  m_numBones;         ///< +0x28
        /// @brief One per bone plus one, identity until set: the rotation that mirrors each bone.
        hkQuaternion*        m_boneMirrorRotations;   ///< +0x30
        /// @brief Bone name → its mirror partner's name, both ways.
        hkStringMap<const char*> m_bonePairNameMap;   ///< +0x38

        virtual ~hkaMirroredSkeleton();

        void mirrorExtractedMotion(hkQsTransform& motion) const;
        /// @brief The bone's mirror partner; -1 for a negative index.
        hkInt16 getBonePair(hkInt16 bone) const;
        /// @brief Mirrors one bone's transform in place.
        /// @note Name provisional.
        void mirrorBone(hkQsTransform& transformInOut, int bone, bool isAdditive) const;
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
        /// @brief A whole local-space pose to model space, parents first.
        /// @note Name per the Havok SDK; out of line in vanilla (0x140bd47b0).
        static void transformLocalPoseToModelPose(int numTransforms, const hkInt16* parentIndices, const hkQsTransform* poseLocal,
                                                  hkQsTransform* poseModelOut);
        /// @brief Copies `numBones` transforms and `numFloats` floats (rounded up to a multiple of four) as 16-byte blocks.
        /// @note Name provisional (0x140bd3ca0); the quantized sampler seeds its outputs with the reference pose through it.
        static void copyPoseAndFloats(hkQsTransform* poseOut, hkReal* floatsOut, const hkQsTransform* pose, int numBones, const hkReal* floats, int numFloats);
    };
} }
