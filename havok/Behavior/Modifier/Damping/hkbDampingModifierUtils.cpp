/// @file
/// @brief hkbDampingModifierUtils, reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/Modifier/Damping/hkbDampingModifierUtils.h>

namespace CB { namespace havok
{
    namespace hkbDampingModifierUtils
    {
        void modify(hkbDampingModifier& mod)
        {
            if (mod.m_enableScalarDamping) {
                const hkReal error = mod.m_rawValue - mod.m_dampedValue;
                const hkReal pTerm = mod.m_kP * error;
                mod.m_errorSum    += error;
                const hkReal iTerm = mod.m_kI * mod.m_errorSum;
                const hkReal dTerm = mod.m_kD * (error - mod.m_previousError);
                mod.m_dampedValue  += pTerm + iTerm + dTerm;
                mod.m_previousError = error;
            }
            if (mod.m_enableVectorDamping) {
                hkVector4 error;
                error.setSub4(mod.m_rawVector, mod.m_dampedVector);
                mod.m_vecErrorSum.add4(error);
                hkVector4 derivative;
                derivative.setSub4(error, mod.m_vecPreviousError);
                hkVector4 iTerm;
                iTerm.setMul4(mod.m_vecErrorSum, hkSimdReal(mod.m_kI));
                hkVector4 dTerm;
                dTerm.setMul4(derivative, hkSimdReal(mod.m_kD));
                mod.m_dampedVector.addMul4(hkSimdReal(mod.m_kP), error);
                mod.m_dampedVector.add4(iTerm);
                mod.m_dampedVector.add4(dTerm);
                mod.m_vecPreviousError = error;
            }
        }
    }
} }
