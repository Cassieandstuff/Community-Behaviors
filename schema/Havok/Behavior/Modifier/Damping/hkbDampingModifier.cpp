/// @file
/// @brief hkbDampingModifier, reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/Modifier/Damping/hkbDampingModifier.h>

#include <Behavior/Modifier/Damping/hkbDampingModifierUtils.h>

namespace CB { namespace schema
{
    /// @note Both constructors set bit 0 of m_userData, as the twist modifier's do.
    hkbDampingModifier::hkbDampingModifier()
    :   m_kP(0.20f),
        m_kI(0.015f),
        m_kD(-0.10f),
        m_enableScalarDamping(true),
        m_enableVectorDamping(false),
        m_rawValue(0.0f),
        m_dampedValue(0.0f),
        m_rawVector(hkVector4::getZero()),
        m_dampedVector(hkVector4::getZero()),
        m_vecErrorSum(hkVector4::getZero()),
        m_vecPreviousError(hkVector4::getZero())
    {
        m_userData |= 1;
        m_errorSum      = 0.0f;
        m_previousError = 0.0f;
    }

    /// @note The 0x30-byte memCpy copies m_kP through m_rawVector.
    hkbDampingModifier::hkbDampingModifier(const hkbDampingModifier& mod)
    :   hkbModifier(mod),
        m_vecErrorSum(hkVector4::getZero()),
        m_vecPreviousError(hkVector4::getZero())
    {
        m_userData |= 1;
        m_errorSum      = 0.0f;
        m_previousError = 0.0f;
        hkString::memCpy(&m_kP, &mod.m_kP, 0x30);
        m_dampedVector.setZero4();
        m_dampedValue = 0.0f;
    }

    void hkbDampingModifier::activate(const hkbContext& context)
    {
        m_dampedValue = 0.0f;
        m_dampedVector.setZero4();
        m_errorSum      = 0.0f;
        m_previousError = 0.0f;
        m_vecErrorSum.setZero4();
        m_vecPreviousError.setZero4();
    }

    hkbNode* hkbDampingModifier::cloneNode(hkbBehaviorGraph& rootBehavior) const
    {
        return new hkbDampingModifier(*this);
    }

    void hkbDampingModifier::modify(const hkbContext& context, hkbGeneratorOutput& inOut)
    {
        HK_TIMER_BEGIN("hkbDampingModifier");
        hkbDampingModifierUtils::modify(*this);
        HK_TIMER_END();
    }

    hkReferencedObject* hkbDampingModifier::createInternalState()
    {
        return new hkbDampingModifierInternalState;
    }

    void hkbDampingModifier::getInternalState(hkReferencedObject& internalState) const
    {
        hkbDampingModifierInternalState& state = static_cast<hkbDampingModifierInternalState&>(internalState);
        state.m_dampedVector     = m_dampedVector;
        state.m_vecErrorSum      = m_vecErrorSum;
        state.m_vecPreviousError = m_vecPreviousError;
        state.m_dampedValue      = m_dampedValue;
        state.m_errorSum         = m_errorSum;
        state.m_previousError    = m_previousError;
    }

    void hkbDampingModifier::setInternalState(const hkReferencedObject& internalState)
    {
        const hkbDampingModifierInternalState& state = static_cast<const hkbDampingModifierInternalState&>(internalState);
        m_dampedVector     = state.m_dampedVector;
        m_vecErrorSum      = state.m_vecErrorSum;
        m_vecPreviousError = state.m_vecPreviousError;
        m_dampedValue      = state.m_dampedValue;
        m_errorSum         = state.m_errorSum;
        m_previousError    = state.m_previousError;
    }
} }
