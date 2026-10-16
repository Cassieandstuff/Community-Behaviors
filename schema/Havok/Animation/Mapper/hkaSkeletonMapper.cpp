/// @file
/// @brief hkaSkeletonMapper, reimplemented against vanilla (AE 1.6.1170).
#include <Animation/Mapper/hkaSkeletonMapper.h>

#include <Animation/Animation/hkaAnimationBinding.h>

namespace CB { namespace schema
{
    hkaSkeletonMapper::~hkaSkeletonMapper()
    {
    }

    /// @note B's tracks are the B bones of the simple mappings whose A bone is animated, then the B chains of the chain
    ///       mappings whose whole A chain is animated, in mapping order.
    void hkaSkeletonMapper::computeTrackMappings(const hkaAnimationBinding* binding, hkArray<hkInt16>& boneToTrackIndicesA,
                                                 hkArray<hkInt16>& boneToTrackIndicesB, hkArray<hkInt16>& trackToBoneIndicesB) const
    {
        const hkaSkeleton* skeletonA = m_mapping.m_skeletonA;
        const hkaSkeleton* skeletonB = m_mapping.m_skeletonB;

        boneToTrackIndicesA.clear();
        boneToTrackIndicesA.setSize(skeletonA->m_bones.getSize(), -1);
        for (hkInt16 track = 0; track < binding->m_transformTrackToBoneIndices.getSize(); track++) {
            boneToTrackIndicesA[binding->m_transformTrackToBoneIndices[track]] = track;
        }

        trackToBoneIndicesB.clear();
        boneToTrackIndicesB.clear();
        boneToTrackIndicesB.setSize(skeletonB->m_bones.getSize(), -1);

        for (hkInt16 i = 0; i < m_mapping.m_simpleMappings.getSize(); i++) {
            const hkaSkeletonMapperData::SimpleMapping& mapping = m_mapping.m_simpleMappings[i];
            if (boneToTrackIndicesA[mapping.m_boneA] >= 0) {
                const hkInt16 boneB = mapping.m_boneB;
                trackToBoneIndicesB.pushBack(boneB);
            }
        }

        for (hkInt16 i = 0; i < m_mapping.m_chainMappings.getSize(); i++) {
            hkArray<hkInt16> chainA;
            hkArray<hkInt16> chainB;
            if (hkaSkeletonUtils::getBoneChain(*skeletonA, m_mapping.m_chainMappings[i].m_startBoneA, m_mapping.m_chainMappings[i].m_endBoneA, chainA) &&
                hkaSkeletonUtils::getBoneChain(*skeletonB, m_mapping.m_chainMappings[i].m_startBoneB, m_mapping.m_chainMappings[i].m_endBoneB, chainB)) {
                bool chainAnimated = true;
                for (int j = 0; j < chainA.getSize(); j++) {
                    if (boneToTrackIndicesA[chainA[j]] < 0) {
                        chainAnimated = false;
                        break;
                    }
                }
                if (chainAnimated) {
                    for (int j = 0; j < chainB.getSize(); j++) {
                        const hkInt16 boneB = chainB[j];
                        trackToBoneIndicesB.pushBack(boneB);
                    }
                }
            }
        }

        boneToTrackIndicesB.setSize(skeletonB->m_bones.getSize(), -1);
        for (hkInt16 track = 0; track < trackToBoneIndicesB.getSize(); track++) {
            boneToTrackIndicesB[trackToBoneIndicesB[track]] = track;
        }
    }
} }
