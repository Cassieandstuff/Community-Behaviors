/// @file
/// @brief hkaSplineCompressedAnimation's sampling, reimplemented against vanilla (AE 1.6.1170).
#include <Animation/Animation/SplineCompressed/hkaSplineCompressedAnimation.h>

namespace CB { namespace schema
{
    void hkaSplineCompressedAnimation::sampleTracks(hkReal time, hkQsTransform* transformTracksOut, hkReal* floatTracksOut, void* cache) const
    {
        samplePartialTracks(time, m_numberOfTransformTracks, transformTracksOut, m_numberOfFloatTracks, floatTracksOut, cache);
    }

    void hkaSplineCompressedAnimation::samplePartialTracks(hkReal time, hkUint32 maxNumTransformTracks, hkQsTransform* transformTracksOut,
                                                           hkUint32 maxNumFloatTracks, hkReal* floatTracksOut, void* cache) const
    {
        HK_TIMER_BEGIN("SampleSpline");

        hkUint32 frame;
        hkReal   delta;
        getFrameAndDelta(time, frame, delta);

        int     block;
        hkReal  blockTime;
        hkUint8 quantizedTime;
        getBlockAndTime(frame, delta, block, blockTime, quantizedTime);

        const hkUint8* dataPtr = computePackedNurbsOffsets(m_data.begin(), m_blockOffsets.begin(), block, m_maskAndQuantizationSize);
        const hkUint8* maskPtr = computePackedNurbsOffsets(m_data.begin(), m_blockOffsets.begin(), block, 0x80000000);
        if (dataPtr && maskPtr) {
            for (hkUint32 i = 0; i < maxNumTransformTracks; i++) {
                const TransformMask& m                 = *reinterpret_cast<const TransformMask*>(maskPtr);
                maskPtr += sizeof(TransformMask);
                const hkUint8        quantizationTypes = m.m_quantizationTypes;
                sampleTranslation(blockTime, quantizedTime, m.m_positionTypes, TrackCompressionParams::ScalarQuantization(quantizationTypes & 3), dataPtr,
                                  transformTracksOut[i].m_translation);
                sampleRotation(blockTime, quantizedTime, m.m_rotationTypes, TrackCompressionParams::RotationQuantization((quantizationTypes >> 2) & 0xf),
                               dataPtr, transformTracksOut[i].m_rotation);
                sampleScale(blockTime, quantizedTime, m.m_scaleTypes, TrackCompressionParams::ScalarQuantization(quantizationTypes >> 6), dataPtr,
                            transformTracksOut[i].m_scale);
            }

            const hkUint32 blockOffset = m_blockOffsets[block];
            dataPtr                    = m_data.begin() + blockOffset + m_floatBlockOffsets[block];
            const hkUint8* floatMask   = m_data.begin() + blockOffset + 4 * m_numberOfTransformTracks;
            for (hkUint32 i = 0; i < maxNumFloatTracks; i++) {
                const hkUint8 floatType = *floatMask++;
                sampleFloat(blockTime, quantizedTime, floatType & 0xf9, TrackCompressionParams::ScalarQuantization((floatType >> 1) & 3), dataPtr,
                            floatTracksOut[i]);
            }
        }

        HK_TIMER_END();
    }

    void hkaSplineCompressedAnimation::sampleIndividualTransformTracks(hkReal time, const hkInt16* tracks, hkUint32 numTracks,
                                                                       hkQsTransform* transformOut) const
    {
        hkUint32 frame;
        hkReal   delta;
        getFrameAndDelta(time, frame, delta);

        int     block;
        hkReal  blockTime;
        hkUint8 quantizedTime;
        getBlockAndTime(frame, delta, block, blockTime, quantizedTime);

        for (hkUint32 i = 0; i < numTracks; i++) {
            const int track = tracks[i];
            if (track >= 0 && track < m_numberOfTransformTracks) {
                const hkUint8* dataPtr =
                    computePackedNurbsOffsets(m_data.begin(), m_blockOffsets.begin(), block, m_transformOffsets[block * m_numberOfTransformTracks + track]);
                const TransformMask* mask =
                    reinterpret_cast<const TransformMask*>(computePackedNurbsOffsets(m_data.begin(), m_blockOffsets.begin(), block, track * 4));
                const hkUint8  quantizationTypes = mask->m_quantizationTypes;
                hkQsTransform& out               = transformOut[i];
                sampleTranslation(blockTime, quantizedTime, mask->m_positionTypes, TrackCompressionParams::ScalarQuantization(quantizationTypes & 3), dataPtr,
                                  out.m_translation);
                sampleRotation(blockTime, quantizedTime, mask->m_rotationTypes, TrackCompressionParams::RotationQuantization((quantizationTypes >> 2) & 0xf),
                               dataPtr, out.m_rotation);
                sampleScale(blockTime, quantizedTime, mask->m_scaleTypes, TrackCompressionParams::ScalarQuantization(quantizationTypes >> 6), dataPtr,
                            out.m_scale);
            } else {
                hkQsTransform& out = transformOut[i];
                out.m_translation.setZero4();
                out.m_rotation.m_vec.setZero4();
                out.m_scale.setZero4();
            }
        }
    }

    void hkaSplineCompressedAnimation::sampleIndividualFloatTracks(hkReal time, const hkInt16* tracks, hkUint32 numTracks, hkReal* out) const
    {
        hkUint32 frame;
        hkReal   delta;
        getFrameAndDelta(time, frame, delta);

        int     block;
        hkReal  blockTime;
        hkUint8 quantizedTime;
        getBlockAndTime(frame, delta, block, blockTime, quantizedTime);

        for (hkUint32 i = 0; i < numTracks; i++) {
            const int track = tracks[i];
            if (track >= 0 && track < m_numberOfFloatTracks) {
                const hkUint8* dataPtr =
                    computePackedNurbsOffsets(m_data.begin(), m_blockOffsets.begin(), block, m_floatOffsets[block * m_numberOfFloatTracks + track]);
                const hkUint8* floatMask = computePackedNurbsOffsets(m_data.begin(), m_blockOffsets.begin(), block, track + 4 * m_numberOfTransformTracks);
                sampleFloat(blockTime, quantizedTime, *floatMask & 0xf9, TrackCompressionParams::ScalarQuantization((*floatMask >> 1) & 3), dataPtr, out[i]);
            } else {
                out[i] = 0.0f;
            }
        }
    }

    int hkaSplineCompressedAnimation::getNumOriginalFrames() const
    {
        return m_numFrames;
    }

    int hkaSplineCompressedAnimation::getNumDataChunks(hkUint32 frame, hkReal delta) const
    {
        return 3;
    }

    void hkaSplineCompressedAnimation::getDataChunks(hkUint32 frame, hkReal delta, DataChunk* dataChunks, int numDataChunks) const
    {
        dataChunks[0].m_data = reinterpret_cast<const hkUint8*>(this);
        dataChunks[0].m_size = sizeof(hkaSplineCompressedAnimation);

        int block = frame / (m_maxFramesPerBlock - 1);
        block     = hkMath::max2(block, 0);
        block     = hkMath::min2(block, m_numBlocks - 1);

        const hkUint32 blockEnd   = (block == m_numBlocks - 1) ? m_data.getSize() : m_blockOffsets[block + 1];
        const hkUint32 blockStart = m_blockOffsets[block];
        dataChunks[1].m_data      = m_data.begin() + blockStart;
        dataChunks[1].m_size      = HK_NEXT_MULTIPLE_OF(16, blockEnd - blockStart);

        dataChunks[2].m_data = reinterpret_cast<const hkUint8*>(m_floatBlockOffsets.begin());
        dataChunks[2].m_size = sizeof(hkUint32);
    }

    int hkaSplineCompressedAnimation::getMaxSizeOfCombinedDataChunks() const
    {
        int maxBlockSize = 0;
        int blockEnd     = m_data.getSize();
        for (int i = m_blockOffsets.getSize() - 1; i >= 0; i--) {
            const int blockSize = blockEnd - m_blockOffsets[i];
            maxBlockSize        = hkMath::max2(maxBlockSize, blockSize);
            blockEnd            = m_blockOffsets[i];
        }
        return HK_NEXT_MULTIPLE_OF(16, maxBlockSize) + HK_NEXT_MULTIPLE_OF(16, int(sizeof(hkaSplineCompressedAnimation))) + 16;
    }

    void hkaSplineCompressedAnimation::samplePartialWithDataChunks(hkUint32 frameIndex, hkReal delta, hkUint32 maxNumTransformTracks,
                                                                   hkQsTransform* transformTracksOut, hkUint32 maxNumFloatTracks, hkReal* floatTracksOut,
                                                                   const DataChunk* dataChunks, int numDataChunks)
    {
        HK_TIMER_BEGIN("SampleSplineChunk");

        const hkaSplineCompressedAnimation* animation = reinterpret_cast<const hkaSplineCompressedAnimation*>(dataChunks[0].m_data);

        int     block;
        hkReal  blockTime;
        hkUint8 quantizedTime;
        animation->getBlockAndTime(frameIndex, delta, block, blockTime, quantizedTime);

        const hkUint8* maskPtr = dataChunks[1].m_data;
        const hkUint8* dataPtr = maskPtr + animation->m_maskAndQuantizationSize;
        for (hkUint32 i = 0; i < maxNumTransformTracks; i++) {
            const TransformMask& m = *reinterpret_cast<const TransformMask*>(maskPtr);
            maskPtr += sizeof(TransformMask);
            const hkUint8 quantizationTypes = m.m_quantizationTypes;
            animation->sampleTranslation(blockTime, quantizedTime, m.m_positionTypes, TrackCompressionParams::ScalarQuantization(quantizationTypes & 3),
                                         dataPtr, transformTracksOut[i].m_translation);
            animation->sampleRotation(blockTime, quantizedTime, m.m_rotationTypes,
                                      TrackCompressionParams::RotationQuantization((quantizationTypes >> 2) & 0xf), dataPtr, transformTracksOut[i].m_rotation);
            animation->sampleScale(blockTime, quantizedTime, m.m_scaleTypes, TrackCompressionParams::ScalarQuantization(quantizationTypes >> 6), dataPtr,
                                   transformTracksOut[i].m_scale);
        }

        dataPtr                  = dataChunks[1].m_data + *reinterpret_cast<const hkUint32*>(dataChunks[2].m_data);
        const hkUint8* floatMask = dataChunks[1].m_data + 4 * animation->m_numberOfTransformTracks;
        for (hkUint32 i = 0; i < maxNumFloatTracks; i++) {
            const hkUint8 floatType = *floatMask++;
            animation->sampleFloat(blockTime, quantizedTime, floatType & 0xf9, TrackCompressionParams::ScalarQuantization((floatType >> 1) & 3), dataPtr,
                                   floatTracksOut[i]);
        }

        HK_TIMER_END();
    }

    hkaSplineCompressedAnimation::~hkaSplineCompressedAnimation()
    {
    }

    void hkaSplineCompressedAnimation::sampleRotation(hkReal blockTime, hkUint8 quantizedTime, hkUint8 mask, TrackCompressionParams::RotationQuantization quantization,
                                                      const hkUint8*& dataInOut, hkQuaternion& rotationOut) const
    {
        readNurbsQuaternion(dataInOut, quantizedTime, m_frameDuration, blockTime, mask, quantization, rotationOut);
        dataInOut = reinterpret_cast<const hkUint8*>(HK_NEXT_MULTIPLE_OF(4, hkUlong(dataInOut)));
    }

    void hkaSplineCompressedAnimation::sampleTranslation(hkReal blockTime, hkUint8 quantizedTime, hkUint8 mask, TrackCompressionParams::ScalarQuantization quantization,
                                                         const hkUint8*& dataInOut, hkVector4& translationOut) const
    {
        if (!mask) {
            translationOut = hkQsTransform::getIdentity().m_translation;
        } else {
            const hkVector4 defaultValue = hkQsTransform::getIdentity().m_translation;
            readNurbsCurve(dataInOut, quantizedTime, m_frameDuration, blockTime, mask, quantization, defaultValue, translationOut);
        }
        dataInOut = reinterpret_cast<const hkUint8*>(HK_NEXT_MULTIPLE_OF(4, hkUlong(dataInOut)));
    }

    void hkaSplineCompressedAnimation::sampleScale(hkReal blockTime, hkUint8 quantizedTime, hkUint8 mask, TrackCompressionParams::ScalarQuantization quantization,
                                                   const hkUint8*& dataInOut, hkVector4& scaleOut) const
    {
        if (!mask) {
            scaleOut = hkQsTransform::getIdentity().m_scale;
        } else {
            const hkVector4 defaultValue = hkQsTransform::getIdentity().m_scale;
            readNurbsCurve(dataInOut, quantizedTime, m_frameDuration, blockTime, mask, quantization, defaultValue, scaleOut);
        }
        dataInOut = reinterpret_cast<const hkUint8*>(HK_NEXT_MULTIPLE_OF(4, hkUlong(dataInOut)));
    }

    void hkaSplineCompressedAnimation::sampleFloat(hkReal blockTime, hkUint8 quantizedTime, hkUint8 mask, TrackCompressionParams::ScalarQuantization quantization,
                                                   const hkUint8*& dataInOut, hkReal& floatOut) const
    {
        if (!mask) {
            floatOut = 0.0f;
        } else {
            const hkVector4 defaultValue(0.0f, 0.0f, 0.0f, 0.0f);
            hkVector4       value;
            readNurbsCurve(dataInOut, quantizedTime, m_frameDuration, blockTime, mask, quantization, defaultValue, value);
            floatOut = value(0);
        }
        dataInOut = reinterpret_cast<const hkUint8*>(HK_NEXT_MULTIPLE_OF(4, hkUlong(dataInOut)));
    }
} }
