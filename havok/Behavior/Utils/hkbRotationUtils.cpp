/// @file
/// @brief hkbRotationUtils, reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/Utils/hkbRotationUtils.h>

namespace CB { namespace havok
{
    namespace
    {
        /// @brief acos, 0 at or above 1 and pi at or below -1.
        /// @note Inlined here (hkMath::acos is the out-of-line copy).
        HK_FORCE_INLINE hkReal safeAcos(hkReal r)
        {
            if (hkMath::fabs(r) >= 1.0f) return (r > 0.0f) ? 0.0f : HK_REAL_PI;
            return ::acosf(r);
        }
    }

    void hkbRotationUtils::rotateToAlignUp(const hkQuaternion& targetRotation, const hkQuaternion& currentRotation, const hkVector4& targetUp,
                                           const hkVector4& currentUp, hkQuaternion& rotationOut, hkReal fraction, hkReal epsilon)
    {
        hkVector4 targetUpWS;
        targetUpWS.setRotatedDir(targetRotation, targetUp);
        hkVector4 currentUpWS;
        currentUpWS.setRotatedDir(currentRotation, currentUp);

        const hkReal angle = safeAcos(currentUpWS.dot3(targetUpWS).getReal());
        if (angle <= epsilon) {
            rotationOut = currentRotation;
        } else if (angle >= HK_REAL_PI - epsilon) {
            rotationOut = targetRotation;
        } else {
            hkVector4 axis;
            axis.setCross(currentUpWS, targetUpWS);
            axis.normalize3();
            hkQuaternion delta;
            delta.setAxisAngle(axis, angle * fraction);
            rotationOut.setMul(delta, currentRotation);
        }
    }
} }
