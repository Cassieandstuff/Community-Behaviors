/// @file
/// @brief hkbMoveCharacterModifier, reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/Modifier/MoveCharacter/hkbMoveCharacterModifierUtils.h>

namespace CB { namespace schema
{
    /// @note VARIANT only because vanilla schedules hkbNode's m_cloneState store later (as in the other modifiers).
    hkbMoveCharacterModifier::hkbMoveCharacterModifier()
    {
        m_offsetPerSecondMS.setZero4();
        m_timeSinceLastModify = 0.0f;
    }

    hkbMoveCharacterModifier::hkbMoveCharacterModifier(const hkbMoveCharacterModifier& mod)
    :   hkbModifier(mod),
        m_offsetPerSecondMS(mod.m_offsetPerSecondMS),
        m_timeSinceLastModify(0.0f)
    {
    }

    void hkbMoveCharacterModifier::activate(const hkbContext& context)
    {
        m_timeSinceLastModify = 0.0f;
    }

    void hkbMoveCharacterModifier::update(const hkbContext& context, hkReal timestep)
    {
        m_timeSinceLastModify += timestep;
    }

    hkbNode* hkbMoveCharacterModifier::cloneNode(hkbBehaviorGraph& rootBehavior) const
    {
        return new hkbMoveCharacterModifier(*this);
    }

    void hkbMoveCharacterModifier::modify(const hkbContext& context, hkbGeneratorOutput& inOut)
    {
        hkbMoveCharacterModifierUtils::modify(*this, inOut);
    }

    hkReferencedObject* hkbMoveCharacterModifier::createInternalState()
    {
        return new hkbMoveCharacterModifierInternalState;
    }

    void hkbMoveCharacterModifier::getInternalState(hkReferencedObject& internalState) const
    {
        static_cast<hkbMoveCharacterModifierInternalState&>(internalState).m_timeSinceLastModify = m_timeSinceLastModify;
    }

    void hkbMoveCharacterModifier::setInternalState(const hkReferencedObject& internalState)
    {
        m_timeSinceLastModify = static_cast<const hkbMoveCharacterModifierInternalState&>(internalState).m_timeSinceLastModify;
    }
} }
