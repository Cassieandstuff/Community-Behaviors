/// @file
/// @brief Workbench: the functions of hkaQuantizedAnimation.cpp still being matched (tools/cbmatch/parked.yaml says why each is
///        stuck). Compiled only by tools/cbmatch/manifests/workbench.yaml; a body that matches graduates back into
///        schema/Havok/Animation/Animation/Quantized/hkaQuantizedAnimation.cpp.
#include <Animation/Animation/Quantized/hkaQuantizedAnimation.h>

namespace CB { namespace schema
{
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

} }
