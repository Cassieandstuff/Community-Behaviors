/// @file
/// @brief hkbTimerModifier, reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/Modifier/Timer/hkbTimerModifier.h>

#include <Behavior/BehaviorGraph/hkbBehaviorGraph.h>

namespace CB { namespace schema
{
    /// @note The alarm fires when its time is crossed in [elapsed, elapsed + timestep). The sender is its own statement
    ///       before the event is built: vanilla reads the payload after the call (the clip's trigger idiom).
    void hkbTimerModifier::update(const hkbContext& context, hkReal timestep)
    {
        const hkReal newTime = m_secondsElapsed + timestep;

        if (m_alarmTimeSeconds >= m_secondsElapsed && m_alarmTimeSeconds < newTime && m_alarmEvent.m_id != -1) {
            hkbCharacter*     character = context.m_character;
            hkbBehaviorGraph* behavior  = character->m_behaviorGraph;
            hkbEventQueue*    queue     = context.m_eventQueue ? context.m_eventQueue : character->m_eventQueue;
            hkbNode*          sender    = behavior->getNodeTemplate(this);
            hkbEvent          event(m_alarmEvent.m_id, m_alarmEvent.m_payload, sender);
            event.m_id = queue->toExternal(event.m_id);
            queue->enqueue(event);
        }

        m_secondsElapsed = newTime;
    }

    hkReferencedObject* hkbTimerModifier::createInternalState()
    {
        return new hkbTimerModifierInternalState;
    }
} }
