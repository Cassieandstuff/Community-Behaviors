/// @file
/// @brief hkaQuantizedAnimation, reimplemented against vanilla (AE 1.6.1170).
#include <Animation/Animation/Quantized/hkaQuantizedAnimation.h>

namespace CB { namespace schema
{
    hkaQuantizedAnimation::~hkaQuantizedAnimation()
    {
    }

    void hkaQuantizedAnimation::sampleFullPose(hkQsTransform* poseOut, hkReal* floatsOut, hkReal* poseWeightsOut, hkReal* floatWeightsOut, hkReal time,
                                               bool additive, hkReal weightThreshold) const
    {
        sampleFullPose(m_data.begin(), poseOut, floatsOut, poseWeightsOut, floatWeightsOut, m_skeleton->m_referencePose.begin(),
                       m_skeleton->m_referenceFloats.begin(), time, additive, weightThreshold);
    }

    void hkaQuantizedAnimation::sampleFullPose(hkQsTransform* poseOut, hkReal* floatsOut, hkReal* poseWeightsOut, hkReal* floatWeightsOut,
                                               hkQsTransform* poseBufferA, hkQsTransform* poseBufferB, hkReal* floatBufferA, hkReal* floatBufferB,
                                               hkReal time, bool additive, hkReal weightThreshold) const
    {
        sampleFullPose(m_data.begin(), poseOut, floatsOut, poseWeightsOut, floatWeightsOut, m_skeleton->m_referencePose.begin(),
                       m_skeleton->m_referenceFloats.begin(), poseBufferA, poseBufferB, floatBufferA, floatBufferB, time, additive, weightThreshold);
    }

    void hkaQuantizedAnimation::sampleTracks(hkReal time, hkQsTransform* transformTracksOut, hkReal* floatTracksOut, hkaChunkCache* cache) const
    {
        const hkaQuantizedAnimationHeader* header       = reinterpret_cast<const hkaQuantizedAnimationHeader*>(m_data.begin());
        const hkUint32                     numBones     = header->m_numBones;
        const hkUint32                     numFloats    = header->m_numFloats;
        const int                          numBonesPad  = HK_NEXT_MULTIPLE_OF(4, numBones);
        const int                          numFloatsPad = HK_NEXT_MULTIPLE_OF(4, numFloats);

        hkTempArray<hkQsTransform> pose(numBonesPad);
        hkTempArray<hkReal>        poseWeights(numBonesPad);
        hkTempArray<hkReal>        floats(numFloatsPad);
        hkTempArray<hkReal>        floatWeights(numFloatsPad);
        sampleFullPose(pose.begin(), floats.begin(), poseWeights.begin(), floatWeights.begin(), time, false, 0.001f);

        for (hkUint32 i = 0; i < numBones; i++) {
            if (poseWeights[i] > 0.0f) {
                *transformTracksOut++ = pose[i];
            }
        }
        for (hkUint32 i = 0; i < numFloats; i++) {
            if (floatWeights[i] > 0.0f) {
                *floatTracksOut = floats[i];
                floatTracksOut++;
            }
        }
    }

    void hkaQuantizedAnimation::samplePartialTracks(hkReal time, hkUint32 maxNumTransformTracks, hkQsTransform* transformTracksOut,
                                                    hkUint32 maxNumFloatTracks, hkReal* floatTracksOut, hkaChunkCache* cache) const
    {
        const hkaQuantizedAnimationHeader* header       = reinterpret_cast<const hkaQuantizedAnimationHeader*>(m_data.begin());
        const hkUint32                     numBones     = header->m_numBones;
        const hkUint32                     numFloats    = header->m_numFloats;
        const int                          numBonesPad  = HK_NEXT_MULTIPLE_OF(4, numBones);
        const int                          numFloatsPad = HK_NEXT_MULTIPLE_OF(4, numFloats);

        hkTempArray<hkQsTransform> pose(numBonesPad);
        hkTempArray<hkReal>        poseWeights(numBonesPad);
        hkTempArray<hkReal>        floats(numFloatsPad);
        hkTempArray<hkReal>        floatWeights(numFloatsPad);
        sampleFullPose(pose.begin(), floats.begin(), poseWeights.begin(), floatWeights.begin(), time, false, 0.001f);

        hkUint32 numSampled = 0;
        for (hkUint32 i = 0; i < numBones; i++) {
            if (numSampled >= maxNumTransformTracks) break;
            if (poseWeights[i] > 0.0f) {
                *transformTracksOut++ = pose[i];
                numSampled++;
            }
        }
        numSampled = 0;
        for (hkUint32 i = 0; i < numFloats; i++) {
            if (floatWeights[i] > 0.0f && numSampled < maxNumFloatTracks) {
                *floatTracksOut = floats[i];
                floatTracksOut++;
                numSampled++;
            }
        }
    }

    void hkaQuantizedAnimation::sampleIndividualTransformTracks(hkReal time, const hkInt16* tracks, hkUint32 numTracks, hkQsTransform* transformOut) const
    {
    }

    void hkaQuantizedAnimation::sampleIndividualFloatTracks(hkReal time, const hkInt16* tracks, hkUint32 numTracks, hkReal* out) const
    {
    }

    int hkaQuantizedAnimation::getNumOriginalFrames() const
    {
        return reinterpret_cast<const hkaQuantizedAnimationHeader*>(m_data.begin())->m_numFrames;
    }

    void hkaQuantizedAnimation::sampleFullPose(const hkUint8* data, hkQsTransform* poseOut, hkReal* floatsOut, hkReal* poseWeightsOut,
                                               hkReal* floatWeightsOut, const hkQsTransform* referencePose, const hkReal* referenceFloats, hkReal time,
                                               bool additive, hkReal weightThreshold)
    {
        const hkaQuantizedAnimationHeader* header = reinterpret_cast<const hkaQuantizedAnimationHeader*>(data);

        hkTempArray<hkQsTransform> poseBufferA(HK_NEXT_MULTIPLE_OF(4, header->m_numBones));
        hkTempArray<hkQsTransform> poseBufferB(HK_NEXT_MULTIPLE_OF(4, header->m_numBones));
        hkTempArray<hkReal>        floatBufferA(HK_NEXT_MULTIPLE_OF(4, header->m_numFloats));
        hkTempArray<hkReal>        floatBufferB(HK_NEXT_MULTIPLE_OF(4, header->m_numFloats));
        sampleFullPose(data, poseOut, floatsOut, poseWeightsOut, floatWeightsOut, referencePose, referenceFloats, poseBufferA.begin(),
                       poseBufferB.begin(), floatBufferA.begin(), floatBufferB.begin(), time, additive, weightThreshold);
    }

    void hkaQuantizedAnimation::sampleFullPose(const hkUint8* data, hkQsTransform* poseOut, hkReal* floatsOut, hkReal* poseWeightsOut,
                                               hkReal* floatWeightsOut, const hkQsTransform* referencePose, const hkReal* referenceFloats,
                                               hkQsTransform* poseBufferA, hkQsTransform* poseBufferB, hkReal* floatBufferA, hkReal* floatBufferB, hkReal time,
                                               bool additive, hkReal weightThreshold)
    {
        const hkaQuantizedAnimationHeader* header = reinterpret_cast<const hkaQuantizedAnimationHeader*>(data);

        const hkUint32 maxFrameIndex = header->m_numFrames - 1;
        const hkReal   frameFloat    = (time / header->m_duration) * maxFrameIndex;
        hkUint32       frame         = static_cast<hkUint32>(frameFloat);
        hkReal         delta;
        if (frame > maxFrameIndex - 1) {
            frame = maxFrameIndex - 1;
            delta = 1.0f;
        } else {
            delta = frameFloat - static_cast<hkReal>(frame);
            if (delta < 0.0f) delta = 0.0f;
            else if (delta >= 1.0f) delta = 1.0f;
        }

        const int frameOffsetA = header->m_frameDataOffset + header->m_frameSize * frame;
        const int frameOffsetB = header->m_frameDataOffset + header->m_frameSize * (frame + 1);
        sampleFrames(data, data + frameOffsetA, data + frameOffsetB, poseOut, floatsOut, poseWeightsOut, floatWeightsOut, referencePose, referenceFloats,
                     poseBufferA, poseBufferB, floatBufferA, floatBufferB, delta, additive, weightThreshold);
    }

    void hkaQuantizedAnimation::sampleFrames(const hkUint8* data, const hkUint8* frameA, const hkUint8* frameB, hkQsTransform* poseOut, hkReal* floatsOut,
                                             hkReal* poseWeightsOut, hkReal* floatWeightsOut, const hkQsTransform* referencePose,
                                             const hkReal* referenceFloats, hkQsTransform* poseBufferA, hkQsTransform* poseBufferB, hkReal* floatBufferA,
                                             hkReal* floatBufferB, hkReal delta, bool additive, hkReal weightThreshold)
    {
        HK_TIMER_BEGIN("QuantizedSampleFullPose");

        const hkaQuantizedAnimationHeader* header            = reinterpret_cast<const hkaQuantizedAnimationHeader*>(data);
        const hkReal                       oneMinusThreshold = 1.0f - weightThreshold;
        const hkUint8*                     frames[2]         = { frameA, frameB };

        for (int pass = 0; pass < 2; pass++) {
            hkQsTransform* pose;
            hkReal*        floats;
            int            frameIndex;
            if (pass == 0) {
                if (delta > weightThreshold) {
                    if (delta >= oneMinusThreshold) continue;
                    hkaSkeletonUtils::copyPoseAndFloats(poseBufferA, floatBufferA, referencePose, header->m_numBones, referenceFloats, header->m_numFloats);
                    frameIndex = 0;
                    pose       = poseBufferA;
                    floats     = floatBufferA;
                } else {
                    hkaSkeletonUtils::copyPoseAndFloats(poseOut, floatsOut, referencePose, header->m_numBones, referenceFloats, header->m_numFloats);
                    frameIndex = 0;
                    pose       = poseOut;
                    floats     = floatsOut;
                }
            } else {
                if (delta <= weightThreshold) continue;
                if (delta < oneMinusThreshold) {
                    hkaSkeletonUtils::copyPoseAndFloats(poseBufferB, floatBufferB, referencePose, header->m_numBones, referenceFloats, header->m_numFloats);
                    pose   = poseBufferB;
                    floats = floatBufferB;
                } else {
                    hkaSkeletonUtils::copyPoseAndFloats(poseOut, floatsOut, referencePose, header->m_numBones, referenceFloats, header->m_numFloats);
                    pose   = poseOut;
                    floats = floatsOut;
                }
                frameIndex = 1;
            }

            const int numStaticScalars = header->m_numStaticScales + header->m_numStaticTranslations;
            scatterScalars(reinterpret_cast<hkReal*>(pose), reinterpret_cast<const hkUint16*>(data + header->m_staticIndicesOffset),
                           reinterpret_cast<const hkReal*>(data + header->m_staticValuesOffset), numStaticScalars);
            const int staticIndicesOffset = header->m_staticIndicesOffset + 2 * numStaticScalars;
            const int staticValuesOffset  = header->m_staticValuesOffset + 4 * numStaticScalars;
            scatterQuaternions(reinterpret_cast<hkVector4*>(pose), reinterpret_cast<const hkUint16*>(data + staticIndicesOffset),
                               reinterpret_cast<const hkUint16*>(data + staticValuesOffset), header->m_numStaticRotations);
            scatterScalars(floats, reinterpret_cast<const hkUint16*>(data + staticIndicesOffset + 2 * header->m_numStaticRotations),
                           reinterpret_cast<const hkReal*>(data + HK_NEXT_MULTIPLE_OF(16, staticValuesOffset + 6 * header->m_numStaticRotations)),
                           header->m_numStaticFloats);

            const int       numDynamicScalars = header->m_numDynamicScales + header->m_numDynamicTranslations;
            const hkUint16* quantized         = reinterpret_cast<const hkUint16*>(frames[frameIndex]);
            sampleDynamicScalars(reinterpret_cast<hkReal*>(pose), reinterpret_cast<const hkUint16*>(data + header->m_dynamicIndicesOffset),
                                 reinterpret_cast<const hkReal*>(data + header->m_dynamicOffsetsOffset),
                                 reinterpret_cast<const hkReal*>(data + header->m_dynamicScalesOffset), quantized, numDynamicScalars);
            const int dynamicIndicesOffset = header->m_dynamicIndicesOffset + 2 * numDynamicScalars;
            scatterQuaternions(reinterpret_cast<hkVector4*>(pose), reinterpret_cast<const hkUint16*>(data + dynamicIndicesOffset), quantized + numDynamicScalars,
                               header->m_numDynamicRotations);
            sampleDynamicScalars(floats, reinterpret_cast<const hkUint16*>(data + dynamicIndicesOffset + 2 * header->m_numDynamicRotations),
                                 reinterpret_cast<const hkReal*>(data + HK_NEXT_MULTIPLE_OF(16, header->m_dynamicOffsetsOffset + 4 * numDynamicScalars)),
                                 reinterpret_cast<const hkReal*>(data + HK_NEXT_MULTIPLE_OF(16, header->m_dynamicScalesOffset + 4 * numDynamicScalars)),
                                 quantized + numDynamicScalars + 3 * header->m_numDynamicRotations, header->m_numDynamicFloats);
        }

        if (delta > weightThreshold && delta < oneMinusThreshold) {
            blendPose(poseOut, poseBufferA, poseBufferB, hkSimdReal(delta), header->m_numBones, 0, additive);
            blendFloats(floatsOut, floatBufferA, floatBufferB, hkSimdReal(delta), header->m_numFloats, 0);
        }

        const hkUint8* weights = data + sizeof(hkaQuantizedAnimationHeader);
        for (int i = 0; i < header->m_numBones; i++) {
            poseWeightsOut[i] = s_weightTable[weights[i]];
        }
        weights += header->m_numBones;
        for (int i = 0; i < header->m_numFloats; i++) {
            floatWeightsOut[i] = s_weightTable[weights[i]];
        }

        HK_TIMER_END();
    }

    void hkaQuantizedAnimation::scatterScalars(hkReal* out, const hkUint16* indices, const hkReal* values, int count)
    {
        for (int i = 0; i < count; i++) {
            out[indices[i]] = values[i];
        }
    }

    void hkaQuantizedAnimation::sampleDynamicScalars(hkReal* out, const hkUint16* indices, const hkReal* offsets, const hkReal* scales,
                                                     const hkUint16* quantized, int count)
    {
        decodeDynamicScalars(out, indices, offsets, scales, quantized, count);
    }

    void hkaQuantizedAnimation::scatterQuaternions(hkVector4* out, const hkUint16* indices, const hkUint16* packed, int count)
    {
        hkVector4 q[4];
        const int numQuads    = count & ~3;
        const int rest        = count - numQuads;
        int       packedIndex = 0;
        int       i           = 0;
        for (; i < numQuads; i += 4) {
            unpackQuaternions4(&q[0], &q[1], &q[2], &q[3], packed + packedIndex);
            packedIndex += 12;
            out[indices[i + 0]] = q[0];
            out[indices[i + 1]] = q[1];
            out[indices[i + 2]] = q[2];
            out[indices[i + 3]] = q[3];
        }
        if (rest) {
            unpackQuaternions4(&q[0], &q[1], &q[2], &q[3], packed + packedIndex);
            for (int j = 0; j < rest; j++) {
                out[indices[i + j]] = q[j];
            }
        }
    }
} }
