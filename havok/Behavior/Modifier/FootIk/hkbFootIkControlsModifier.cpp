/// @file
/// @brief hkbFootIkControlsModifier, reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/Modifier/FootIk/hkbFootIkControlsModifier.h>

#include <Behavior/BehaviorGraph/hkbBehaviorGraph.h>
#include <Behavior/Character/hkbCharacter.h>
#include <Behavior/Context/hkbContext.h>
#include <Behavior/Event/hkbEventQueue.h>
#include <Behavior/Modifier/FootIk/hkbFootIkControlsModifierUtils.h>

namespace CB { namespace havok
{
    hkbFootIkControlsModifier::hkbFootIkControlsModifier(const hkbFootIkControlsModifier& mod)
    :   hkbModifier(mod),
        m_alignWithGroundRotation(hkQuaternion::getIdentity())
    {
        m_controlData = mod.m_controlData;
        const int numLegs = m_legs.getSize();
        for (int i = 0; i < numLegs; i++) {
            m_legs[i].m_groundPosition.setZero4();
            m_legs[i].m_verticalError = 0.0f;
            m_legs[i].m_hitSomething  = false;
            m_legs[i].m_isPlantedMS   = false;
        }
    }

    hkbNode* hkbFootIkControlsModifier::cloneNode(hkbBehaviorGraph& rootBehavior) const
    {
        if (m_variableBindingSet == HK_NULL) {
            hkbFootIkControlsModifier* self = const_cast<hkbFootIkControlsModifier*>(this);
            self->m_cloneState              = CLONE_STATE_SHARABLE;
            addReference();
            return self;
        }
        return new hkbFootIkControlsModifier(*this);
    }

    void hkbFootIkControlsModifier::activate(const hkbContext& context)
    {
        hkbFootIkDriver* driver = context.m_character->m_footIkDriver;
        if (driver) {
            const hkArray<hkbFootIkDriver::InternalLegData>& legs = driver->getInternalLegData();
            hkbFootIkControlsModifierUtils::copyLegs(*this, legs.begin(), legs.getSize());
        }
    }

    void hkbFootIkControlsModifier::update(const hkbContext& context, hkReal timestep)
    {
        hkbFootIkDriver* driver = context.m_character->m_footIkDriver;
        if (driver) {
            const hkArray<hkbFootIkDriver::InternalLegData>& legs = driver->getInternalLegData();
            hkbFootIkControlsModifierUtils::copyLegs(*this, legs.begin(), legs.getSize());
        }

        hkbEventQueue* queue = context.m_eventQueue ? context.m_eventQueue : context.m_character->m_eventQueue;
        const int numLegs = m_legs.getSize();
        for (int i = 0; i < numLegs; i++) {
            const Leg& leg = m_legs[i];
            if (leg.m_isPlantedMS && !leg.m_hitSomething && leg.m_ungroundedEvent.m_id != -1) {
                hkbNode* sender = context.m_character->m_behaviorGraph->getNodeTemplate(this);
                hkbEvent event(leg.m_ungroundedEvent.m_id, leg.m_ungroundedEvent.m_payload, sender);
                event.m_id = queue->toExternal(event.m_id);
                queue->enqueue(event);
            }
        }
    }

    void hkbFootIkControlsModifier::modify(const hkbContext& context, hkbGeneratorOutput& inOut)
    {
        HK_TIMER_BEGIN("FootIkControlsModifier");
        const hkbFootIkDriver::InternalLegData* legs    = HK_NULL;
        int                                     numLegs = 0;
        hkbFootIkDriver*                        driver  = context.m_character->m_footIkDriver;
        if (driver) {
            legs    = driver->getInternalLegData().begin();
            numLegs = driver->getInternalLegData().getSize();
        }
        hkbFootIkControlsModifierUtils::modify(*this, inOut, legs, numLegs);
        HK_TIMER_END();
    }
} }
