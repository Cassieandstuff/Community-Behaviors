/// @file
/// @brief hkbComputeDirectionModifier, reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/Modifier/ComputeDirection/hkbComputeDirectionModifier.h>

#include <Behavior/Modifier/ComputeDirection/hkbComputeDirectionModifierUtils.h>

namespace CB { namespace havok
{
    hkbComputeDirectionModifier::hkbComputeDirectionModifier()
    :   m_pointIn(hkVector4::getZero()),
        m_pointOut(hkVector4::getZero()),
        m_groundAngleOut(0.0f),
        m_upAngleOut(0.0f),
        m_verticalOffset(0.0f),
        m_reverseGroundAngle(false),
        m_reverseUpAngle(false),
        m_projectPoint(false),
        m_normalizePoint(false),
        m_computeOnlyOnce(false),
        m_computedOutput(false)
    {
    }

    /// @note The 9-byte memCpy copies m_verticalOffset through m_computeOnlyOnce.
    hkbComputeDirectionModifier::hkbComputeDirectionModifier(const hkbComputeDirectionModifier& mod)
    :   hkbModifier(mod),
        m_pointIn(mod.m_pointIn),
        m_pointOut(hkVector4::getZero()),
        m_groundAngleOut(0.0f),
        m_upAngleOut(0.0f)
    {
        m_computedOutput = false;
        hkString::memCpy(&m_verticalOffset, &mod.m_verticalOffset, 9);
    }

    void hkbComputeDirectionModifier::activate(const hkbContext& context)
    {
        m_pointOut.setZero4();
        m_groundAngleOut = 0.0f;
        m_upAngleOut     = 0.0f;
        m_computedOutput = false;
    }

    hkbNode* hkbComputeDirectionModifier::cloneNode(hkbBehaviorGraph& rootBehavior) const
    {
        return new hkbComputeDirectionModifier(*this);
    }

    void hkbComputeDirectionModifier::modify(const hkbContext& context, hkbGeneratorOutput& inOut)
    {
        const hkbCharacterData* data = context.m_character->m_setup->m_data;
        hkbComputeDirectionModifierUtils::modify(data->m_modelUpMS, data->m_modelForwardMS, data->m_modelRightMS, inOut, *this);
    }

    hkReferencedObject* hkbComputeDirectionModifier::createInternalState()
    {
        return new hkbComputeDirectionModifierInternalState;
    }

    void hkbComputeDirectionModifier::getInternalState(hkReferencedObject& internalState) const
    {
        hkbComputeDirectionModifierInternalState& state = static_cast<hkbComputeDirectionModifierInternalState&>(internalState);
        state.m_pointOut       = m_pointOut;
        state.m_groundAngleOut = m_groundAngleOut;
        state.m_upAngleOut     = m_upAngleOut;
        state.m_computedOutput = m_computedOutput;
    }

    void hkbComputeDirectionModifier::setInternalState(const hkReferencedObject& internalState)
    {
        const hkbComputeDirectionModifierInternalState& state = static_cast<const hkbComputeDirectionModifierInternalState&>(internalState);
        m_pointOut       = state.m_pointOut;
        m_groundAngleOut = state.m_groundAngleOut;
        m_upAngleOut     = state.m_upAngleOut;
        m_computedOutput = state.m_computedOutput;
    }
} }
