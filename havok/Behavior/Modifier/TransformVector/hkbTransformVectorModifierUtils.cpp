/// @file
/// @brief hkbTransformVectorModifierUtils, reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/Modifier/TransformVector/hkbTransformVectorModifierUtils.h>

namespace CB { namespace havok
{
    namespace hkbTransformVectorModifierUtils
    {
        /// @note The rotation is hkVector4::setRotatedDir / setRotatedInverseDir written out once with only the cross
        ///       product chosen by m_inverse: vanilla shares everything but the cross products between the two cases,
        ///       which two inlined calls do not give.
        void computeVectorOut(hkbTransformVectorModifier& modifier)
        {
            if (modifier.m_rotateOnly) {
                const hkVector4& quat      = modifier.m_rotation.m_vec;
                const hkVector4& direction = modifier.m_vectorIn;
                const hkSimdReal qreal      = quat.getSimdAt3();
                const hkSimdReal imagDotDir = quat.dot3(direction);
                hkVector4 cross;
                if (modifier.m_inverse) cross.setCross(direction, quat);
                else                    cross.setCross(quat, direction);
                const hkSimdReal q2minus1(_mm_add_ps(_mm_mul_ps(qreal.m_real, qreal.m_real), g_vectorConstantMinusHalf.m_quad));
                hkVector4 ret;
                ret.setMul4(direction, q2minus1);
                ret.addMul4(imagDotDir, quat);
                ret.addMul4(cross, qreal);
                modifier.m_vectorOut.setAdd4(ret, ret);
            }
            else {
                const hkQsTransform transform(modifier.m_translation, modifier.m_rotation);
                if (modifier.m_inverse) modifier.m_vectorOut.setTransformedInversePos(transform, modifier.m_vectorIn);
                else                    modifier.m_vectorOut.setTransformedPos(transform, modifier.m_vectorIn);
            }
        }
    }
} }
