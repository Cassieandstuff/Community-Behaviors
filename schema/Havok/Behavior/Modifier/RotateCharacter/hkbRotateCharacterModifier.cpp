/// @file
/// @brief hkbRotateCharacterModifier, reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/Modifier/RotateCharacter/hkbRotateCharacterModifier.h>

#include <Behavior/Generator/hkbGeneratorOutput.h>
#include <Behavior/Modifier/RotateCharacter/hkbRotateCharacterModifierUtils.h>

namespace CB { namespace schema
{
    hkbRotateCharacterModifier::hkbRotateCharacterModifier()
    :   m_degreesPerSecond(90.0f),
        m_speedMultiplier(1.0f)
    {
        m_axisOfRotation.set(1.0f, 0.0f, 0.0f);
        m_angle = 0.0f;
    }

    hkbRotateCharacterModifier::hkbRotateCharacterModifier(const hkbRotateCharacterModifier& mod)
    :   hkbModifier(mod),
        m_degreesPerSecond(mod.m_degreesPerSecond),
        m_speedMultiplier(mod.m_speedMultiplier),
        m_axisOfRotation(mod.m_axisOfRotation),
        m_angle(0.0f)
    {
    }

    void hkbRotateCharacterModifier::activate(const hkbContext& context)
    {
        m_angle = 0.0f;
    }

    void hkbRotateCharacterModifier::update(const hkbContext& context, hkReal timestep)
    {
        m_angle += m_degreesPerSecond * HK_REAL_DEG_TO_RAD * m_speedMultiplier * timestep;
    }

    hkbNode* hkbRotateCharacterModifier::cloneNode(hkbBehaviorGraph& rootBehavior) const
    {
        return new hkbRotateCharacterModifier(*this);
    }

    hkReferencedObject* hkbRotateCharacterModifier::createInternalState()
    {
        return new hkbRotateCharacterModifierInternalState;
    }

    void hkbRotateCharacterModifier::getInternalState(hkReferencedObject& internalState) const
    {
        static_cast<hkbRotateCharacterModifierInternalState&>(internalState).m_angle = m_angle;
    }

    void hkbRotateCharacterModifier::setInternalState(const hkReferencedObject& internalState)
    {
        m_angle = static_cast<const hkbRotateCharacterModifierInternalState&>(internalState).m_angle;
    }

    void hkbRotateCharacterModifier::modify(const hkbContext& context, hkbGeneratorOutput& inOut)
    {
        HK_TIMER_BEGIN("hkbRotateCharacterModifier");
        hkbCharacter* character = context.m_character;
        hkbRotateCharacterModifierUtils::modify(*this, *character->m_worldFromModel, character->m_setup->m_data->m_modelUpMS,
                                                *static_cast<hkQsTransform*>(inOut.getTrackData(hkbGeneratorOutput::TRACK_WORLD_FROM_MODEL)));
        HK_TIMER_END();
    }
} }
