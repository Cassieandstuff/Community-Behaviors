/// @file
/// @brief hkbEventsFromRangeModifier, reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/Modifier/EventsFromRange/hkbEventsFromRangeModifier.h>

#include <Behavior/BehaviorGraph/hkbBehaviorGraph.h>
#include <Behavior/Event/hkbEventQueue.h>

namespace CB { namespace havok
{
    /// @note Not in the inventory (only .pdata references it); the TU's first function.
    hkbEventsFromRangeModifier::hkbEventsFromRangeModifier()
    :   m_inputValue(0.0f),
        m_lowerBound(0.0f)
    {
    }

    hkbEventsFromRangeModifier::hkbEventsFromRangeModifier(const hkbEventsFromRangeModifier& mod)
    :   hkbModifier(mod),
        m_eventRanges(mod.m_eventRanges)
    {
    }

    /// @note Every range starts inactive.
    void hkbEventsFromRangeModifier::activate(const hkbContext& context)
    {
        if (!m_eventRanges) return;

        const int numRanges = m_eventRanges->m_eventData.getSize();
        m_wasActiveInPreviousFrame.setSize(0);
        m_wasActiveInPreviousFrame.reserveExactly(numRanges);
        m_wasActiveInPreviousFrame.setSize(numRanges, false);
    }

    /// @note The first range whose upper bound reaches the input is the active one (none below the lower bound). Its
    ///       event is raised on entry, or every update in EVENT_MODE_SEND_WHEN_IN_RANGE.
    void hkbEventsFromRangeModifier::update(const hkbContext& context, hkReal timestep)
    {
        if (!m_eventRanges || m_eventRanges->m_eventData.getSize() <= 0) return;

        const bool aboveLowerBound = m_lowerBound <= m_inputValue;
        bool       foundRange      = false;
        for (int i = 0; i < m_eventRanges->m_eventData.getSize(); i++) {
            const hkbEventRangeData& range = m_eventRanges->m_eventData[i];
            if (m_inputValue > range.m_upperBound || !aboveLowerBound || foundRange) {
                m_wasActiveInPreviousFrame[i] = false;
            } else {
                if ((range.m_eventMode == hkbEventRangeData::EVENT_MODE_SEND_WHEN_IN_RANGE ||
                     (!m_wasActiveInPreviousFrame[i] && range.m_eventMode == hkbEventRangeData::EVENT_MODE_SEND_ON_ENTER_RANGE)) &&
                    range.m_event.m_id != -1) {
                    hkbEventQueue* queue  = context.getEventQueue();
                    hkbNode*       sender = context.m_character->m_behaviorGraph->getNodeTemplate(this);
                    hkbEvent       event(range.m_event.m_id, range.m_event.m_payload, sender);
                    event.m_id = queue->toExternal(event.m_id);
                    queue->enqueue(event);
                }
                foundRange                    = true;
                m_wasActiveInPreviousFrame[i] = true;
            }
        }
    }

    bool hkbEventsFromRangeModifier::isValid(hkStringPtr& errorString) const
    {
        if (m_eventRanges) {
            for (int i = 0; i < m_eventRanges->m_eventData.getSize(); i++) {
                if (i == 0) {
                    if (m_eventRanges->m_eventData[0].m_upperBound <= m_lowerBound) {
                        errorString = "The first range's upper bound must be larger than the lower bound.";
                        return false;
                    }
                } else if (m_eventRanges->m_eventData[i].m_upperBound <= m_eventRanges->m_eventData[i - 1].m_upperBound) {
                    errorString = "Each range's upper bound must be larger than the previous range's.";
                    return false;
                }
            }
        }
        return true;
    }

    hkbNode* hkbEventsFromRangeModifier::cloneNode(hkbBehaviorGraph& rootBehavior) const
    {
        return new hkbEventsFromRangeModifier(*this);
    }

    hkReferencedObject* hkbEventsFromRangeModifier::createInternalState()
    {
        return new hkbEventsFromRangeModifierInternalState;
    }

    void hkbEventsFromRangeModifier::getInternalState(hkReferencedObject& internalState) const
    {
        hkbEventsFromRangeModifierInternalState& state = static_cast<hkbEventsFromRangeModifierInternalState&>(internalState);
        state.m_wasActiveInPreviousFrame               = m_wasActiveInPreviousFrame;
    }

    void hkbEventsFromRangeModifier::setInternalState(const hkReferencedObject& internalState)
    {
        const hkbEventsFromRangeModifierInternalState& state = static_cast<const hkbEventsFromRangeModifierInternalState&>(internalState);
        m_wasActiveInPreviousFrame                           = state.m_wasActiveInPreviousFrame;
    }
} }
