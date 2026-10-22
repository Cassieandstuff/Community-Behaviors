/// @file
/// @brief hkbTwistModifierUtils, reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/Modifier/Twist/hkbTwistModifierUtils.h>
#include <Behavior/Generator/hkbGeneratorOutputUtils.h>
#include <Behavior/Generator/Clip/hkbClipGenerator.h>
#include <Behavior/Generator/hkbGeneratorOutput.h>

namespace CB { namespace havok
{
    namespace hkbTwistModifierUtils
    {
        /// @note In model coordinates the axis is taken into the chain's local frame through the accumulated rotation of
        ///       the start bone's ancestors and of the chain bones already twisted. RAMPED divides the running angle by
        ///       (i + 0.5) at every bone, cumulatively — vanilla's arithmetic, kept as is.
        void modifyInternal(const hkbTwistModifier& modifier, hkbGeneratorOutput& inOut, const hkQsTransform* referencePose)
        {
            const int numPoseLocal = inOut.getNumPoseLocal();
            if (modifier.m_startBoneIndex >= 0 && modifier.m_endBoneIndex >= 0 &&
                (modifier.m_startBoneIndex < numPoseLocal || modifier.m_endBoneIndex < numPoseLocal) &&
                modifier.m_boneChainIndices.getSize() > 0)
            {
                hkbGeneratorOutputUtils::makePoseDense(inOut, referencePose);
                hkQsTransform* pose = inOut.accessPoseLocal();

                hkQuaternion parentRotation = hkQuaternion::getIdentity();
                if (modifier.m_rotationAxisCoordinates == hkbTwistModifier::ROTATION_AXIS_IN_MODEL_COORDINATES) {
                    for (int i = 0; i < modifier.m_parentBoneIndices.getSize(); i++)
                        parentRotation.setMul(pose[modifier.m_parentBoneIndices[i]].m_rotation, parentRotation);
                }

                const int    numBones    = modifier.m_boneChainIndices.getSize();
                const hkReal invNumBones = 1.0f / numBones;
                hkReal       angle       = modifier.m_twistAngle * HK_REAL_DEG_TO_RAD * invNumBones;
                if (modifier.m_setAngleMethod == hkbTwistModifier::RAMPED) angle *= invNumBones * 2.0f;

                for (int i = 0; i < numBones; i++) {
                    if (modifier.m_setAngleMethod == hkbTwistModifier::RAMPED) angle /= (i + 0.5f);

                    hkVector4 axis = modifier.m_axisOfRotation;
                    if (modifier.m_rotationAxisCoordinates == hkbTwistModifier::ROTATION_AXIS_IN_MODEL_COORDINATES) {
                        hkQuaternion inverse;
                        inverse.setInverse(parentRotation);
                        axis.setRotatedDir(inverse, axis);
                        axis.normalize3();
                    }

                    hkQuaternion twist;
                    twist.setAxisAngle(axis, angle);

                    hkQsTransform&     bone         = pose[modifier.m_boneChainIndices[i]];
                    const hkQuaternion boneRotation = bone.m_rotation;
                    hkQuaternion       rotation;
                    if (modifier.m_isAdditive) rotation.setMul(twist, boneRotation);
                    else                       rotation = twist;
                    bone.m_rotation = rotation;

                    if (modifier.m_rotationAxisCoordinates == hkbTwistModifier::ROTATION_AXIS_IN_MODEL_COORDINATES)
                        parentRotation.setMul(parentRotation, rotation);
                }
            }
        }
    }
} }
