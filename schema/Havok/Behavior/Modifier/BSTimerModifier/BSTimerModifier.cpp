/// @file
/// @brief BSTimerModifier, reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/Modifier/BSTimerModifier/BSTimerModifier.h>

namespace CB { namespace schema
{
    BSTimerModifier::BSTimerModifier()
    :   m_alarmTimeSeconds(0.0f),
        m_resetAlarm(false),
        m_secondsElapsed(0.0f)
    {
        m_userData |= 2;
    }

    BSTimerModifier::BSTimerModifier(hkFinishLoadedObjectFlag flag)
    :   hkbModifier(flag),
        m_alarmEvent(flag)
    {
        m_userData |= 2;
    }

    BSTimerModifier::BSTimerModifier(const BSTimerModifier& mod)
    :   hkbModifier(mod),
        m_alarmTimeSeconds(mod.m_alarmTimeSeconds),
        m_alarmEvent(mod.m_alarmEvent),
        m_resetAlarm(mod.m_resetAlarm),
        m_secondsElapsed(mod.m_secondsElapsed)
    {
        m_userData |= 2;
    }

    const void* BSTimerModifier::getClassType() const
    {
        return &staticClass();
    }

    void BSTimerModifier::activate(const hkbContext& context)
    {
        m_secondsElapsed = 0.0f;
    }

    /// @note The alarm fires when its time is crossed in [elapsed, elapsed + timestep), sent by the node itself (no
    ///       getNodeTemplate, unlike hkbTimerModifier). A re-arming alarm subtracts its time even when it has no event.
    void BSTimerModifier::update(const hkbContext& context, hkReal timestep)
    {
        hkReal newTime = m_secondsElapsed + timestep;

        if (m_alarmTimeSeconds >= m_secondsElapsed && m_alarmTimeSeconds < newTime) {
            if (m_alarmEvent.m_id != -1) {
                hkbEventQueue* queue = context.getEventQueue();
                hkbEvent       event(m_alarmEvent.m_id, m_alarmEvent.m_payload, this);
                event.m_id = queue->toExternal(event.m_id);
                queue->enqueue(event);
            }
            if (m_resetAlarm) newTime -= m_alarmTimeSeconds;
        }

        m_secondsElapsed = newTime;
    }

    hkbNode* BSTimerModifier::cloneNode(hkbBehaviorGraph& rootBehavior) const
    {
        return new BSTimerModifier(*this);
    }
} }
