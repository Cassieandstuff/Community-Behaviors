/// @file
/// @brief hkbGetWorldFromModelModifier, reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/Modifier/GetWorldFromModel/hkbGetWorldFromModelModifier.h>

#include <Behavior/Generator/hkbGeneratorOutput.h>

namespace CB { namespace schema
{
    /// @note VARIANT only because vanilla schedules hkbNode's m_cloneState store later (as in the other modifiers).
    hkbGetWorldFromModelModifier::hkbGetWorldFromModelModifier()
    {
        m_translationOut = hkVector4::getZero();
        m_rotationOut    = hkQuaternion::getIdentity();
    }

    hkbGetWorldFromModelModifier::hkbGetWorldFromModelModifier(const hkbGetWorldFromModelModifier& mod)
    :   hkbModifier(mod)
    {
        m_translationOut = hkVector4::getZero();
        m_rotationOut    = hkQuaternion::getIdentity();
    }

    hkbNode* hkbGetWorldFromModelModifier::cloneNode(hkbBehaviorGraph& rootBehavior) const
    {
        return new hkbGetWorldFromModelModifier(*this);
    }

    void hkbGetWorldFromModelModifier::modify(const hkbContext& context, hkbGeneratorOutput& inOut)
    {
        m_translationOut = inOut.accessWorldFromModel().m_translation;
        m_rotationOut    = inOut.accessWorldFromModel().m_rotation;
    }

    hkReferencedObject* hkbGetWorldFromModelModifier::createInternalState()
    {
        return new hkbGetWorldFromModelModifierInternalState;
    }

    void hkbGetWorldFromModelModifier::getInternalState(hkReferencedObject& internalState) const
    {
        hkbGetWorldFromModelModifierInternalState& state = static_cast<hkbGetWorldFromModelModifierInternalState&>(internalState);
        state.m_translationOut = m_translationOut;
        state.m_rotationOut    = m_rotationOut;
    }

    void hkbGetWorldFromModelModifier::setInternalState(const hkReferencedObject& internalState)
    {
        const hkbGetWorldFromModelModifierInternalState& state =
            static_cast<const hkbGetWorldFromModelModifierInternalState&>(internalState);
        m_translationOut = state.m_translationOut;
        m_rotationOut    = state.m_rotationOut;
    }
} }
