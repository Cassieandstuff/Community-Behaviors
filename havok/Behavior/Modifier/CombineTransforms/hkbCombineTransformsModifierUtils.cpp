/// @file
/// @brief hkbCombineTransformsModifierUtils, reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/Modifier/CombineTransforms/hkbCombineTransformsModifierUtils.h>

namespace CB { namespace havok
{
    namespace hkbCombineTransformsModifierUtils
    {
        /// @note Spelling closest to vanilla's code (EXACT): each side is built in hkQuadReal temporaries and only then
        ///       copied into the locals whose addresses go to multiply (an address-taken hkVector4 is stored before the
        ///       flip and again after it); the sign masks are float literals (movaps, where the epi32 form loads with
        ///       movdqa) that vanilla hoists above both sides; the flags compare as ints.
        void modify(hkbCombineTransformsModifier& modifier)
        {
            const int        invertResult = modifier.m_invertResult;
            const hkQuadReal negateAll    = _mm_set1_ps(-0.0f);
            const hkQuadReal negateImag   = _mm_setr_ps(-0.0f, -0.0f, -0.0f, 0.0f);

            hkQuadReal leftT = modifier.m_leftTranslation.m_quad;
            hkQuadReal leftR = modifier.m_leftRotation.m_vec.m_quad;
            if (int(modifier.m_invertLeftTransform) != invertResult) {
                leftT = _mm_xor_ps(leftT, negateAll);
                leftR = _mm_xor_ps(leftR, negateImag);
            }
            hkQuaternion leftRotation;
            leftRotation.m_vec.m_quad = leftR;
            hkVector4 leftTranslation(leftT);

            hkQuadReal rightT = modifier.m_rightTranslation.m_quad;
            hkQuadReal rightR = modifier.m_rightRotation.m_vec.m_quad;
            if (int(modifier.m_invertRightTransform) != invertResult) {
                rightT = _mm_xor_ps(rightT, negateAll);
                rightR = _mm_xor_ps(rightR, negateImag);
            }
            hkQuaternion rightRotation;
            rightRotation.m_vec.m_quad = rightR;
            hkVector4 rightTranslation(rightT);

            if (modifier.m_invertResult) multiply(rightTranslation, rightRotation, leftTranslation, leftRotation, modifier.m_translationOut, modifier.m_rotationOut);
            else                         multiply(leftTranslation, leftRotation, rightTranslation, rightRotation, modifier.m_translationOut, modifier.m_rotationOut);
        }

        /// @note VARIANT (SIMD scheduling, --diff agrees): vanilla shuffles translationB before rotationA in the cross
        ///       product and splats the quaternion reals in the other order.
        void multiply(const hkVector4& translationA, const hkQuaternion& rotationA, const hkVector4& translationB, const hkQuaternion& rotationB,
                      hkVector4& translationOut, hkQuaternion& rotationOut)
        {
            hkVector4 rotated;
            rotated.setRotatedDir(rotationA, translationB);
            translationOut.setAdd4(rotated, translationA);
            rotationOut.setMul(rotationA, rotationB);
        }
    }
} }
