/// @file
/// @brief hkaSplineCompressedAnimation's sampling, reimplemented against vanilla (AE 1.6.1170).
#include <Animation/Animation/SplineCompressed/hkaSplineCompressedAnimation.h>

namespace CB { namespace schema
{
    void hkaSplineCompressedAnimation::sampleTracks(hkReal time, hkQsTransform* transformTracksOut, hkReal* floatTracksOut, hkaChunkCache* cache) const
    {
        samplePartialTracks(time, m_numberOfTransformTracks, transformTracksOut, m_numberOfFloatTracks, floatTracksOut, cache);
    }

    void hkaSplineCompressedAnimation::samplePartialTracks(hkReal time, hkUint32 maxNumTransformTracks, hkQsTransform* transformTracksOut,
                                                           hkUint32 maxNumFloatTracks, hkReal* floatTracksOut, hkaChunkCache* cache) const
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

    void hkaSplineCompressedAnimation::readNurbsQuaternion(const hkUint8*& dataInOut, hkUint8 quantizedTime, hkReal frameDuration, hkReal time, hkUint8 mask,
                                                           TrackCompressionParams::RotationQuantization quantization, hkQuaternion& out)
    {
        if (mask & 0xf0) {
            int       n;
            int       p;
            hkReal    U[2 * MAX_DEGREE];
            hkVector4 P[MAX_DEGREE + 1];
            const int span = readKnots(dataInOut, n, p, quantizedTime, frameDuration, U);
            readPackedQuaternions(dataInOut, quantization, n, p, span, P);
            s_evaluate[p](time, p, U, P, out.m_vec);
        } else if (mask & 0x0f) {
            const int align = s_rotationAlign[quantization];
            dataInOut       = reinterpret_cast<const hkUint8*>((hkUlong(dataInOut) + (align - 1)) & ~(align - 1));
            s_unpackQuaternion[quantization](dataInOut, &out.m_vec);
            dataInOut += s_rotationSize[quantization];
        } else {
            out.setIdentity();
        }
    }

    int hkaSplineCompressedAnimation::readKnots(const hkUint8*& dataInOut, int& n, int& p, hkUint8 quantizedTime, hkReal frameDuration, hkReal* U)
    {
        const int numControlPointsMinusOne = *reinterpret_cast<const hkUint16*>(dataInOut);
        dataInOut += sizeof(hkUint16);
        const int degree = *dataInOut;
        dataInOut += sizeof(hkUint8);

        const hkUint8* knots = dataInOut;
        const int      span  = findSpan(numControlPointsMinusOne, degree, quantizedTime, knots);
        for (int i = 0; i < 2 * degree; i++) {
            U[i] = hkReal(knots[span - degree + 1 + i]) * frameDuration;
        }
        dataInOut = knots + (numControlPointsMinusOne + degree + 2);

        n = numControlPointsMinusOne;
        p = degree;
        return span;
    }

    void hkaSplineCompressedAnimation::readPackedQuaternions(const hkUint8*& dataInOut, TrackCompressionParams::RotationQuantization quantization, int n, int p,
                                                             int span, hkVector4* out)
    {
        const int align = s_rotationAlign[quantization];
        dataInOut       = reinterpret_cast<const hkUint8*>((hkUlong(dataInOut) + (align - 1)) & ~(align - 1));
        const int size  = s_rotationSize[quantization];
        for (int i = 0; i <= p; i++) {
            s_unpackQuaternion[quantization](dataInOut + (span - p + i) * size, &out[i]);
        }
        dataInOut += (n + 1) * size;
    }

    void hkaSplineCompressedAnimation::evaluate(hkReal u, int p, const hkReal* U, const hkVector4* P, hkVector4& out)
    {
        hkReal N[MAX_DEGREE + 1];
        hkReal right[MAX_DEGREE + 1];
        hkReal left[MAX_DEGREE + 1];

        N[0] = 1.0f;
        for (int j = 1; j <= p; j++) {
            left[j]      = u - U[p - j];
            right[j]     = U[p + j - 1] - u;
            hkReal saved = 0.0f;
            for (int r = 0; r < j; r++) {
                const hkReal temp = N[r] / (right[r + 1] + left[j - r]);
                N[r]              = saved + right[r + 1] * temp;
                saved             = left[j - r] * temp;
            }
            N[j] = saved;
        }

        hkVector4 sum;
        sum.setZero4();
        for (int i = 0; i <= p; i++) {
            sum.addMul4(hkSimdReal(N[i]), P[i]);
        }
        out = sum;
    }

    void hkaSplineCompressedAnimation::evaluateSimple1(hkReal u, int p, const hkReal* U, const hkVector4* P, hkVector4& out)
    {
        const hkSimdReal left  = u - U[0];
        const hkSimdReal right = U[1] - u;
        const hkSimdReal t     = left / (right + left);
        out.setInterpolate4(P[0], P[1], t);
    }

    /// @note Cox–de Boor (evaluate's basis) unrolled by hand, on broadcast reals.
    void hkaSplineCompressedAnimation::evaluateSimple2(hkReal u, int p, const hkReal* U, const hkVector4* P, hkVector4& out)
    {
        const hkSimdReal right1 = U[2] - u;
        const hkSimdReal left1  = u - U[1];
        hkSimdReal       temp   = hkSimdReal(1.0f) / (right1 + left1);
        const hkSimdReal N0     = temp * right1;
        const hkSimdReal N1     = temp * left1;

        const hkSimdReal left2  = u - U[0];
        const hkSimdReal right2 = U[3] - u;
        temp                    = N0 / (right1 + left2);
        const hkSimdReal M0     = temp * right1;
        const hkSimdReal saved  = temp * left2;
        temp                    = N1 / (right2 + left1);
        const hkSimdReal M1     = saved + temp * right2;
        const hkSimdReal M2     = temp * left1;

        hkVector4 sum;
        sum.setMul4(P[0], M0);
        sum.addMul4(P[1], M1);
        sum.addMul4(P[2], M2);
        out = sum;
    }

    /// @note As evaluateSimple2, one degree further.
    void hkaSplineCompressedAnimation::evaluateSimple3(hkReal u, int p, const hkReal* U, const hkVector4* P, hkVector4& out)
    {
        const hkSimdReal right1 = U[3] - u;
        const hkSimdReal left1  = u - U[2];
        hkSimdReal       temp   = hkSimdReal(1.0f) / (right1 + left1);
        const hkSimdReal N0     = temp * right1;
        const hkSimdReal N1     = temp * left1;

        const hkSimdReal left2  = u - U[1];
        const hkSimdReal right2 = U[4] - u;
        temp                    = N0 / (right1 + left2);
        const hkSimdReal O0     = temp * right1;
        hkSimdReal       saved  = temp * left2;
        temp                    = N1 / (right2 + left1);
        const hkSimdReal O1     = saved + temp * right2;
        const hkSimdReal O2     = temp * left1;

        const hkSimdReal left3  = u - U[0];
        const hkSimdReal right3 = U[5] - u;
        temp                    = O0 / (right1 + left3);
        const hkSimdReal M0     = temp * right1;
        saved                   = temp * left3;
        temp                    = O1 / (right2 + left2);
        const hkSimdReal M1     = saved + temp * right2;
        saved                   = temp * left2;
        temp                    = O2 / (right3 + left1);
        const hkSimdReal M2     = saved + temp * right3;
        const hkSimdReal M3     = temp * left1;

        hkVector4 sum;
        sum.setMul4(P[0], M0);
        sum.addMul4(P[1], M1);
        sum.addMul4(P[2], M2);
        sum.addMul4(P[3], M3);
        out = sum;
    }
} }
