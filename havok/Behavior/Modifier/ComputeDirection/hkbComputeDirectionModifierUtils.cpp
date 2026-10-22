/// @file
/// @brief hkbComputeDirectionModifierUtils, reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/Modifier/ComputeDirection/hkbComputeDirectionModifierUtils.h>

#include <Behavior/Generator/hkbGeneratorOutput.h>

namespace CB { namespace havok
{
    namespace hkbComputeDirectionModifierUtils
    {
        void modify(const hkVector4& up, const hkVector4& forward, const hkVector4& right, hkbGeneratorOutput& inOut,
                    hkbComputeDirectionModifier& mod)
        {
            if (inOut.getNumPoseLocal() != 0 && (!mod.m_computedOutput || !mod.m_computeOnlyOnce)) {
                mod.m_pointOut.setTransformedInversePos(inOut.accessWorldFromModel(), mod.m_pointIn);
                mod.m_pointOut.addMul4(hkSimdReal(mod.m_verticalOffset), up);

                const hkReal sideways = mod.m_pointOut.dot3(right).getReal();
                const hkReal ahead    = mod.m_pointOut.dot3(forward).getReal();
                mod.m_groundAngleOut  = hkMath::atan2fApproximation(sideways, ahead) * HK_REAL_RAD_TO_DEG;

                const hkReal length = mod.m_pointOut.length3().getReal();
                if (length == 0.0f) {
                    mod.m_upAngleOut = 0.0f;
                } else {
                    const hkReal sine = mod.m_pointOut.dot3(up).getReal() / length;
                    mod.m_upAngleOut  = hkMath::asin(sine) * HK_REAL_RAD_TO_DEG;
                }

                if (mod.m_reverseGroundAngle) mod.m_groundAngleOut = -mod.m_groundAngleOut;
                if (mod.m_reverseUpAngle)     mod.m_upAngleOut     = -mod.m_upAngleOut;
                if (mod.m_projectPoint)       mod.m_pointOut.subMul4(hkSimdReal(mod.m_pointOut.dot3(up).getReal()), up);
                if (mod.m_normalizePoint)     mod.m_pointOut.normalize3IfNotZero();

                mod.m_computedOutput = true;
            }
        }
    }
} }
