/// @file
/// @brief Workbench: the functions of hkaSplineCompressedAnimation.cpp still being matched (tools/cbmatch/parked.yaml says why each is
///        stuck). Compiled only by tools/cbmatch/manifests/workbench.yaml; a body that matches graduates back into
///        havok/Animation/Animation/SplineCompressed/hkaSplineCompressedAnimation.cpp.
#include <Animation/Animation/SplineCompressed/hkaSplineCompressedAnimation.h>

namespace CB { namespace havok
{
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

} }
