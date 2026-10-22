/// @file
/// @brief Workbench: the functions of hkbClipGenerator.cpp still being matched (tools/cbmatch/parked.yaml says why each is
///        stuck). Compiled only by tools/cbmatch/manifests/workbench.yaml; a body that matches graduates back into
///        havok/Behavior/Generator/Clip/hkbClipGenerator.cpp.
#include <Behavior/Generator/Clip/hkbClipGenerator.h>
#include <Behavior/Generator/hkbGeneratorOutputUtils.h>
#include <Animation/Rig/hkaSkeleton.h>
#include <Common/Base/Memory/Util/Stats/hkStatisticsCollector.h>

namespace CB { namespace havok
{
    /// @note Vanilla's own shapes: `min2(lod, numTracks)` (cmovl into numTracks), the echo weights summed under a
    ///       fresh `i < size` test rather than `!isClip`, the control re-read through m_animationControl around
    ///       getFutureTime, and the normalize pass dividing by a literal 1 (the echo weights already sum to 1).
    void hkbClipGenerator::sampleAnimation(const hkbContext& context, hkQsTransform* poseLocal, int numPoseLocal, hkReal* floatSlots,
                                           int numFloatSlots, hkaChunkCache* animationCache, hkBitField& partitionsSampled)
    {
        const hkaAnimationBinding* binding   = m_animationControl->m_binding;
        const hkaAnimation*        animation = binding ? binding->m_animation : hkRefPtr<hkaAnimation>();
        if (!animation) return;

        int numTracks = m_mapperData ? animation->m_numberOfTransformTracks
                                     : hkaSkeletonUtils::findLastUsedTrack(binding->m_transformTrackToBoneIndices.m_data, HK_NULL, numPoseLocal,
                                                                           animation->m_numberOfTransformTracks) + 1;
        const hkInt16 numTracksInLod = context.m_character->m_numTracksInLod;
        if (numTracksInLod > -1) numTracks = hkMath::min2(int(numTracksInLod), numTracks);

        hkQsTransform* tracks      = hkAllocateStack<hkQsTransform>(numTracks);
        const int      numFloats   = (numFloatSlots > 0) ? animation->m_numberOfFloatTracks : 0;
        hkReal*        floatTracks = hkAllocateStack<hkReal>(numFloats);

        if (m_echos.getSize() == 0) {
            animation->samplePartialTracks(m_localTime, numTracks, tracks, numFloats, floatTracks, animationCache);
            if (numFloatSlots > 0) copySampledFloats(numFloats, floatTracks, numFloatSlots, binding, floatSlots);
        } else {
            hkString::memSet(tracks, 0, numTracks * int(sizeof(hkQsTransform)));
            hkReal totalWeight = 0.0f;
            for (int i = 0; i < m_echos.getSize() + 1; i++) {
                hkQsTransform* echoTracks = hkAllocateStack<hkQsTransform>(numTracks);
                const bool     isClip     = (i == m_echos.getSize());
                const hkReal   offset     = isClip ? 0.0f : m_echos[i].m_offsetLocalTime;

                hkReal       localTime;
                int          loops;
                const hkReal speed                  = m_animationControl->m_playbackSpeed;
                m_animationControl->m_playbackSpeed = 1.0f;
                m_animationControl->getFutureTime(offset, localTime, loops);
                m_animationControl->m_playbackSpeed = speed;
                if (m_mode == MODE_SINGLE_PLAY && loops != 0)
                    localTime = m_animationControl->m_binding->m_animation->m_duration + m_animationControl->m_cropStartAmountLocalTime;

                const hkReal weight = isClip ? 1.0f - totalWeight : m_echos[i].m_weight;
                totalWeight += (i < m_echos.getSize()) ? weight : 0.0f;

                animation->samplePartialTracks(localTime, numTracks, echoTracks, numFloats, floatTracks, animationCache);
                for (hkInt16 j = 0; j < numTracks; j++) tracks[j].blendAddMul(echoTracks[j], hkSimdReal(weight));
                if (numFloatSlots > 0) blendSampledFloats(numFloats, floatTracks, numFloatSlots, binding, weight, floatSlots);

                hkDeallocateStack(echoTracks, numTracks);
            }
            for (hkInt16 j = 0; j < numTracks; j++) tracks[j].blendNormalize(hkSimdReal(1.0f));
        }

        const hkaMirroredSkeleton* mirror = (m_flags & FLAG_MIRROR) ? context.m_character->m_setup->getMirroredSkeleton() : HK_NULL;

        hkQsTransform* mapped;
        int            numMapped = numTracks;
        if (const hkaDefaultAnimationControlMapperData* mapperData = m_mapperData) {
            const hkaSkeletonMapper* mapper = mapperData->m_mapper;
            numMapped                       = mapperData->m_trackToBoneIndicesB.getSize();
            mapped                          = hkAllocateStack<hkQsTransform>(numMapped);
            const hkaSkeleton* skeletonB    = mapper->m_mapping.m_skeletonB;
            for (hkInt16 j = 0; j < numMapped; j++) mapped[j] = skeletonB->m_referencePose[mapperData->m_trackToBoneIndicesB[j]];
            mapper->mapPoseLocalSpace(tracks, mapped, m_mapperData->m_boneToTrackIndicesA.m_data, m_mapperData->m_boneToTrackIndicesB.m_data,
                                      m_animationControl->m_binding->m_blendHint == hkaAnimationBinding::ADDITIVE);
        } else {
            mapped = tracks;
        }

        copySampledPose(numMapped, mapped, numPoseLocal, poseLocal, partitionsSampled,
                            (m_mapperData == HK_NULL) ? binding->m_transformTrackToBoneIndices : m_mapperData->m_trackToBoneIndicesB,
                            binding->m_blendHint == hkaAnimationBinding::ADDITIVE, mirror, true);

        if (m_mapperData) hkDeallocateStack(mapped, numMapped);
        hkDeallocateStack(floatTracks, numFloats);
        hkDeallocateStack(tracks, numTracks);
    }

} }
