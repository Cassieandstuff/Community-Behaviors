/// @file
/// @brief hkaSkeletonMapper: maps poses and extracted motion between two skeletons.
#pragma once

#include <Animation/Rig/hkaSkeleton.h>

namespace CB { namespace havok
{
    struct hkaAnimationBinding;

    /// @brief How skeleton A's bones map onto skeleton B's: bone to bone, chain to chain, and the bones left unmapped.
    /// @note 0x80 bytes.
    struct hkaSkeletonMapperData
    {
        enum MappingType { HK_RAGDOLL_MAPPING = 0, HK_RETARGETING_MAPPING = 1 };

        /// @note 0x40 bytes.
        struct SimpleMapping
        {
            hkInt16       m_boneA;             ///< +0x00
            hkInt16       m_boneB;             ///< +0x02
            hkQsTransform m_aFromBTransform;   ///< +0x10
        };

        /// @note 0x70 bytes.
        struct ChainMapping
        {
            hkInt16       m_startBoneA;             ///< +0x00
            hkInt16       m_endBoneA;               ///< +0x02
            hkInt16       m_startBoneB;             ///< +0x04
            hkInt16       m_endBoneB;               ///< +0x06
            hkQsTransform m_startAFromBTransform;   ///< +0x10
            hkQsTransform m_endAFromBTransform;     ///< +0x40
        };

        hkRefPtr<hkaSkeleton>         m_skeletonA;                ///< +0x00
        hkRefPtr<hkaSkeleton>         m_skeletonB;                ///< +0x08
        hkArray<SimpleMapping>        m_simpleMappings;           ///< +0x10
        hkArray<ChainMapping>         m_chainMappings;            ///< +0x20
        hkArray<hkInt16>              m_unmappedBones;            ///< +0x30
        hkQsTransform                 m_extractedMotionMapping;   ///< +0x40
        hkBool                        m_keepUnmappedLocal;        ///< +0x70
        hkEnum<MappingType, hkInt32>  m_mappingType;              ///< +0x74

        /// @note Out of line in vanilla (0x140c2a900).
        hkaSkeletonMapperData(hkFinishLoadedObjectFlag flag);
        /// @note Out of line in vanilla (0x140c2a910): hkaSkeletonMapper's destructor calls it.
        ~hkaSkeletonMapperData();
    };

    struct hkaSkeletonMapper : hkReferencedObject
    {
        /// @brief Where mapPose takes the constraints for B's unmapped bones from.
        /// @note Values per the Havok SDK.
        enum ConstraintSource { NO_CONSTRAINTS = 0, REFERENCE_POSE = 1, CURRENT_POSE = 2 };

        hkaSkeletonMapperData m_mapping;   ///< +0x10

        HK_FORCE_INLINE hkaSkeletonMapper(hkFinishLoadedObjectFlag flag) : hkReferencedObject(flag), m_mapping(flag) {}
        virtual ~hkaSkeletonMapper();

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
        /// @brief Fills the tables a control needs to play `binding` (bound to skeleton A) on skeleton B: A's and B's bone →
        ///        track maps and B's track → bone map.
        /// @note Name provisional (hkaDefaultAnimationControlMapperData's constructor is its only caller).
        void computeTrackMappings(const hkaAnimationBinding* binding, hkArray<hkInt16>& boneToTrackIndicesA, hkArray<hkInt16>& boneToTrackIndicesB,
                                  hkArray<hkInt16>& trackToBoneIndicesB) const;
    };
} }
