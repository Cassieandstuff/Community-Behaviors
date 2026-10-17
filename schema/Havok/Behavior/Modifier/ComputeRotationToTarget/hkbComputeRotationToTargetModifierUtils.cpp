/// @file
/// @brief hkbComputeRotationToTargetModifierUtils, reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/Modifier/ComputeRotationToTarget/hkbComputeRotationToTargetModifierUtils.h>

namespace CB { namespace schema
{
    namespace hkbComputeRotationToTargetModifierUtils
    {
        void modify(hkbComputeRotationToTargetModifier& modifier)
        {
            const hkVector4& axis = modifier.m_localAxisOfRotation;

            hkVector4 toTargetWS;
            toTargetWS.setSub4(modifier.m_targetPosition, modifier.m_currentPosition);
            hkVector4 toTarget;
            toTarget.setRotatedInverseDir(modifier.m_currentRotation, toTargetWS);
            toTarget.subMul4(toTarget.dot3(axis), axis);
            toTarget.normalize3();
            if (!toTarget.isOk3()) return;

            hkVector4 facing = modifier.m_localFacingDirection;
            facing.subMul4(facing.dot3(axis), axis);
            facing.normalize3();
            if (!facing.isOk3()) return;

            hkQuaternion rotation;
            if (facing.dot3(toTarget).getReal() >= -0.9999f) rotation.setShortestRotation(facing, toTarget);
            else                                              rotation.m_vec = axis;

            if (modifier.m_resultIsDelta) modifier.m_rotationOut = rotation;
            else                          modifier.m_rotationOut.setMul(modifier.m_currentRotation, rotation);
        }
    }
} }
