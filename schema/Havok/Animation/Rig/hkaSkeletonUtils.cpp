/// @file
/// @brief hkaSkeletonUtils, reimplemented against vanilla (AE 1.6.1170): the functions matched so far.
#include <Animation/Rig/hkaSkeleton.h>

namespace CB { namespace schema
{
    void hkaSkeletonUtils::getModelSpaceScale(const hkaSkeleton& skeleton, const hkQsTransform* poseLocal, int boneId, hkVector4& scaleOut)
    {
        hkVector4 scale = g_vectorConstantOne;
        while (boneId != -1) {
            scale.setMul4(scale, poseLocal[boneId].m_scale);
            boneId = skeleton.m_parentIndices[boneId];
        }
        scaleOut = scale;
    }
} }
