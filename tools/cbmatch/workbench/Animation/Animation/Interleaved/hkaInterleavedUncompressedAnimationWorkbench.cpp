/// @file
/// @brief Workbench: the functions of hkaInterleavedUncompressedAnimation.cpp still being matched (tools/cbmatch/parked.yaml says why each is
///        stuck). Compiled only by tools/cbmatch/manifests/workbench.yaml; a body that matches graduates back into
///        schema/Havok/Animation/Animation/Interleaved/hkaInterleavedUncompressedAnimation.cpp.
#include <Animation/Animation/Interleaved/hkaInterleavedUncompressedAnimation.h>

namespace CB { namespace schema
{
    void hkaInterleavedUncompressedAnimation::transformTrack(int trackIndex, const hkQsTransform& transform)
    {
        const int numFrames = m_transforms.getSize() / m_numberOfTransformTracks;
        for (int f = 0; f < numFrames; f++) {
            hkQsTransform& t = m_transforms[m_numberOfTransformTracks * f + trackIndex];
            t.setMul(transform, t);
        }
    }

    void hkaInterleavedUncompressedAnimation::getDataChunks(hkUint32 frame, hkReal delta, DataChunk* dataChunks, int numDataChunks) const
    {
        dataChunks[0].m_data = reinterpret_cast<const hkUint8*>(this);
        dataChunks[0].m_size = sizeof(hkaInterleavedUncompressedAnimation);
        int chunk            = 1;

        if (m_numberOfTransformTracks > 0) {
            const int numTracks      = m_numberOfTransformTracks;
            dataChunks[chunk].m_data = reinterpret_cast<const hkUint8*>(m_transforms.m_data + numTracks * frame);
            dataChunks[chunk].m_size = sizeof(hkQsTransform) * (2 * numTracks);
            chunk++;
        }
        if (m_numberOfFloatTracks > 0) {
            const int numTracks      = m_numberOfFloatTracks;
            dataChunks[chunk].m_data = reinterpret_cast<const hkUint8*>(m_floats.m_data + numTracks * frame);
            dataChunks[chunk].m_size = sizeof(hkReal) * (2 * numTracks);
        }
    }

    void hkaInterleavedUncompressedAnimation::samplePartialWithDataChunks(hkUint32 frameIndex, hkReal delta, hkUint32 maxNumTransformTracks,
                                                                          hkQsTransform* transformTracksOut, hkUint32 maxNumFloatTracks,
                                                                          hkReal* floatTracksOut, const DataChunk* dataChunks, int numDataChunks)
    {
        HK_TIMER_BEGIN("SampleInterleavedChunk");

        const hkaInterleavedUncompressedAnimation* animation = reinterpret_cast<const hkaInterleavedUncompressedAnimation*>(dataChunks[0].m_data);
        int                                        chunk     = 1;

        if (animation->m_numberOfTransformTracks > 0) {
            const hkQsTransform* current = reinterpret_cast<const hkQsTransform*>(dataChunks[1].m_data);
            const hkQsTransform* next    = current + animation->m_numberOfTransformTracks;
            for (hkUint32 i = 0; i < maxNumTransformTracks; i++) {
                transformTracksOut[i].setInterpolate4(*current, *next, delta);
                current++;
                next++;
            }
            chunk = 2;
        }

        if (animation->m_numberOfFloatTracks > 0) {
            const hkReal  oneMinusDelta = 1.0f - delta;
            const hkReal* current       = reinterpret_cast<const hkReal*>(dataChunks[chunk].m_data);
            const hkReal* next          = current + animation->m_numberOfFloatTracks;
            for (hkUint32 i = 0; i < maxNumFloatTracks; i++) {
                floatTracksOut[i] = next[i] * delta + current[i] * oneMinusDelta;
            }
        }

        HK_TIMER_END();
    }

} }
