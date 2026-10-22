/// @file
/// @brief BSEventEveryNEventsModifier, reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/Modifier/BSEventEveryNEventsModifier/BSEventEveryNEventsModifier.h>

#include <Behavior/BehaviorGraph/hkbBehaviorGraph.h>

namespace CB { namespace havok
{
    BSEventEveryNEventsModifier::BSEventEveryNEventsModifier()
    :   m_numberOfEventsBeforeSend(1),
        m_minimumNumberOfEventsBeforeSend(1),
        m_randomizeNumberOfEvents(false),
        m_numberOfEventsSeen(0),
        m_calculatedNumberOfEventsBeforeSend(1)
    {
        m_userData |= 3;
    }

    /// @note The calculated N restarts from the loaded N.
    BSEventEveryNEventsModifier::BSEventEveryNEventsModifier(hkFinishLoadedObjectFlag flag)
    :   hkbModifier(flag),
        m_eventToCheckFor(flag),
        m_eventToSend(flag)
    {
        m_userData |= 3;
        m_calculatedNumberOfEventsBeforeSend = m_numberOfEventsBeforeSend;
    }

    /// @note Copies the running count and the calculated N too.
    BSEventEveryNEventsModifier::BSEventEveryNEventsModifier(const BSEventEveryNEventsModifier& mod)
    :   hkbModifier(mod),
        m_eventToCheckFor(mod.m_eventToCheckFor),
        m_eventToSend(mod.m_eventToSend),
        m_numberOfEventsBeforeSend(mod.m_numberOfEventsBeforeSend),
        m_minimumNumberOfEventsBeforeSend(mod.m_minimumNumberOfEventsBeforeSend),
        m_randomizeNumberOfEvents(mod.m_randomizeNumberOfEvents),
        m_numberOfEventsSeen(mod.m_numberOfEventsSeen),
        m_calculatedNumberOfEventsBeforeSend(mod.m_calculatedNumberOfEventsBeforeSend)
    {
        m_userData |= 3;
    }

    void BSEventEveryNEventsModifier::calculateNumberOfEventsBeforeSend(const hkbContext& context)
    {
        hkbBehaviorGraph*        behavior = context.m_character->m_behaviorGraph;
        hkPseudoRandomGenerator* random   = behavior ? behavior->m_pseudoRandomGenerator : HK_NULL;
        if (random) {
            const int range = m_numberOfEventsBeforeSend - m_minimumNumberOfEventsBeforeSend + 1;
            m_calculatedNumberOfEventsBeforeSend = hkInt8(random->getRandChar(range) + m_minimumNumberOfEventsBeforeSend);
        }
    }

    const void* BSEventEveryNEventsModifier::getClassType() const
    {
        return &staticClass();
    }

    void BSEventEveryNEventsModifier::activate(const hkbContext& context)
    {
        if (m_calculatedNumberOfEventsBeforeSend <= 0) m_calculatedNumberOfEventsBeforeSend = 1;
        m_numberOfEventsSeen = 0;
        if (m_randomizeNumberOfEvents) calculateNumberOfEventsBeforeSend(context);
    }

    /// @note Matches the event's raw id. Every Nth match sends m_eventToSend (from the node itself) and, when
    ///       randomized, draws the next N.
    void BSEventEveryNEventsModifier::handleEvent(const hkbContext& context, const hkbEvent e)
    {
        if (e.m_id == m_eventToCheckFor.m_id) {
            m_numberOfEventsSeen = (m_numberOfEventsSeen + 1) % m_calculatedNumberOfEventsBeforeSend;
            if (m_numberOfEventsSeen == 0) {
                if (context.getEventQueue()) {
                    hkbEventQueue* queue = context.getEventQueue();
                    hkbEvent       event(m_eventToSend.m_id, m_eventToSend.m_payload, this);
                    event.m_id = queue->toExternal(event.m_id);
                    queue->enqueue(event);
                }
                if (m_randomizeNumberOfEvents) calculateNumberOfEventsBeforeSend(context);
            }
        }
    }

    bool BSEventEveryNEventsModifier::isValid(hkStringPtr& errorString) const
    {
        if (m_numberOfEventsBeforeSend <= 0) {
            errorString = "m_numberOfEventsBeforeSend has to be greater than zero.";
            return false;
        }
        if (m_minimumNumberOfEventsBeforeSend <= 0) {
            errorString = "m_minimumNumberOfEventsBeforeSend has to be greater than zero.";
            return false;
        }
        if (m_minimumNumberOfEventsBeforeSend > m_numberOfEventsBeforeSend) {
            errorString = "m_minimumNumberOfEventsBeforeSend has to be less than or equal to m_numberOfEventsBeforeSend.";
            return false;
        }
        if (m_eventToCheckFor.m_id == -1) {
            errorString = "Event to check for has to be valid.";
            return false;
        }
        if (m_eventToSend.m_id == -1) {
            errorString = "Event to send has to be valid.";
            return false;
        }
        return true;
    }

    hkbNode* BSEventEveryNEventsModifier::cloneNode(hkbBehaviorGraph& rootBehavior) const
    {
        return new BSEventEveryNEventsModifier(*this);
    }
} }
