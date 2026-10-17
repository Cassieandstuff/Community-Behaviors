/// @file
/// @brief hkaInterleavedUncompressedAnimation, reimplemented against vanilla (AE 1.6.1170).
#include <Animation/Animation/Interleaved/hkaInterleavedUncompressedAnimation.h>

namespace CB { namespace schema
{
    hkaInterleavedUncompressedAnimation::hkaInterleavedUncompressedAnimation(const hkaInterleavedUncompressedAnimation& other)
    :   hkaAnimation(other)
    {
        m_transforms = other.m_transforms;
        m_floats     = other.m_floats;
    }

    void hkaInterleavedUncompressedAnimation::sampleTracks(hkReal time, hkQsTransform* transformTracksOut, hkReal* floatTracksOut, hkaChunkCache* cache) const
    {
        samplePartialTracks(time, m_numberOfTransformTracks, transformTracksOut, m_numberOfFloatTracks, floatTracksOut, cache);
    }

    void hkaInterleavedUncompressedAnimation::samplePartialTracks(hkReal time, hkUint32 maxNumTransformTracks, hkQsTransform* transformTracksOut,
                                                                  hkUint32 maxNumFloatTracks, hkReal* floatTracksOut, hkaChunkCache* cache) const
    {
        HK_TIMER_BEGIN("samplePose");

        if (time >= m_duration) {
            for (hkUint32 i = 0; i < maxNumTransformTracks; i++) {
                transformTracksOut[i] = m_transforms[m_transforms.getSize() - m_numberOfTransformTracks + i];
            }
            for (hkUint32 i = 0; i < maxNumFloatTracks; i++) {
                floatTracksOut[i] = m_floats[m_floats.getSize() - m_numberOfFloatTracks + i];
            }
        } else {
            hkUint32 frame;
            hkReal   delta;
            getFrameAndDelta(time, frame, delta);

            DataChunk chunks[3];
            getDataChunks(frame, delta, chunks, 3);
            samplePartialWithDataChunks(frame, delta, maxNumTransformTracks, transformTracksOut, maxNumFloatTracks, floatTracksOut, chunks, 3);
        }

        HK_TIMER_END();
    }

    void hkaInterleavedUncompressedAnimation::sampleIndividualTransformTracks(hkReal time, const hkInt16* tracks, hkUint32 numTracks,
                                                                              hkQsTransform* transformOut) const
    {
        hkUint32 frame;
        hkReal   delta;
        getFrameAndDelta(time, frame, delta);

        for (hkUint32 i = 0; i < numTracks; i++) {
            const hkInt16 track = tracks[i];
            if (track >= 0 && track < m_numberOfTransformTracks) {
                const hkQsTransform* current = &m_transforms[frame * m_numberOfTransformTracks];
                const hkQsTransform  t0      = current[track];
                const hkQsTransform  t1      = current[track + m_numberOfTransformTracks];
                transformOut[i].setInterpolate4(t0, t1, delta);
            }
        }
    }

    void hkaInterleavedUncompressedAnimation::sampleIndividualFloatTracks(hkReal time, const hkInt16* tracks, hkUint32 numTracks, hkReal* out) const
    {
        hkUint32 frame;
        hkReal   delta;
        getFrameAndDelta(time, frame, delta);

        const hkReal oneMinusDelta = 1.0f - delta;
        for (hkUint32 i = 0; i < numTracks; i++) {
            const hkInt16 track = tracks[i];
            if (track >= 0 && track < m_numberOfFloatTracks) {
                const hkReal* current = &m_floats[frame * m_numberOfFloatTracks];
                out[i]                = delta * current[track + m_numberOfFloatTracks] + oneMinusDelta * current[track];
            }
        }
    }

    int hkaInterleavedUncompressedAnimation::getNumOriginalFrames() const
    {
        if (m_numberOfTransformTracks) return m_transforms.getSize() / m_numberOfTransformTracks;
        return m_floats.getSize() / m_numberOfFloatTracks;
    }

    int hkaInterleavedUncompressedAnimation::getNumDataChunks(hkUint32 frame, hkReal delta) const
    {
        int numChunks = (m_numberOfTransformTracks > 0) ? 2 : 1;
        if (m_numberOfFloatTracks > 0) numChunks++;
        return numChunks;
    }

    int hkaInterleavedUncompressedAnimation::getMaxSizeOfCombinedDataChunks() const
    {
        return HK_NEXT_MULTIPLE_OF(16, int(sizeof(hkaInterleavedUncompressedAnimation))) + 2 * m_numberOfTransformTracks * int(sizeof(hkQsTransform))
               + HK_NEXT_MULTIPLE_OF(16, 2 * m_numberOfFloatTracks * int(sizeof(hkReal))) + 16;
    }

} }
