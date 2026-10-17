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

    /// @note The bone is range-checked unsigned and the enabled byte tested as unsigned `> 0` (vanilla: movzx/cwde/jnb, then
    ///       `cmp byte,0; ja`); the hkBool conversion or a signed compare emits jnz / jnl.
    int hkaSkeletonUtils::findLastUsedTrack(const hkInt16* trackToBone, const hkBool* tracksEnabled, int numBones, int numTracks)
    {
        if (!trackToBone) return hkMath::min2(numTracks - 1, numBones - 1);
        for (int i = numTracks - 1; i >= 0; i--) {
            const hkInt16 bone = trackToBone[i];
            if (bone >= 0 && hkUint32(bone) < hkUint32(numBones) && (!tracksEnabled || hkUint8(tracksEnabled[i].m_bool) > 0)) return i;
        }
        return -1;
    }
} }
