/// @file
/// @brief Workbench: the functions of hkbStateMachine.cpp still being matched (tools/cbmatch/parked.yaml says why each is
///        stuck). Compiled only by tools/cbmatch/manifests/workbench.yaml; a body that matches graduates back into
///        schema/Havok/Behavior/StateMachine/hkbStateMachine.cpp.
#include <Behavior/StateMachine/hkbStateMachine.h>
#include <Behavior/BehaviorGraph/hkbBehaviorGraph.h>
#include <Behavior/Condition/hkbCondition.h>
#include <Behavior/Context/hkbContext.h>
#include <Behavior/Event/hkbEventQueue.h>
#include <Behavior/Generator/hkbGeneratorOutputUtils.h>
#include <Behavior/StateMachine/hkbStateChooser.h>
#include <Behavior/TransitionEffect/hkbTransitionEffect.h>
#include <Common/Base/Container/LocalArray/hkLocalArray.h>
#include <Common/Base/Memory/Util/Stats/hkStatisticsCollector.h>
#include <Common/Base/Monitor/hkMonitorStream.h>

namespace CB { namespace schema
{
    void hkbStateMachine::update(const hkbContext& context, hkReal timestep)
    {
        if (!m_isActive) return;

        HK_TIMER_BEGIN_LIST("hkbStateMachine::update", "UpdateStateListeners");
        hkbBehaviorGraph* rootBehavior = context.m_character->m_behaviorGraph;
        m_states[m_sCurrentStateIndexAndEntered >> 1]->updateListeners(context, timestep, this);
        m_timeInState += timestep;

        HK_TIMER_SPLIT_LIST("UpdateActiveTransitions");
        updateActiveTransitions(context);

        HK_TIMER_SPLIT_LIST("UpdateTransitions");
        const bool fromGeneratorAtEnd = isFromGeneratorAtEnd(rootBehavior, timestep);
        processDelayedTransitions(context, fromGeneratorAtEnd);

        hkbEventQueue* queue = context.getEventQueue();
        if (m_stateOrTransitionChanged) {
            if (m_eventToSendWhenStateOrTransitionChanges.m_id != -1) {
                const hkbEventProperty e(m_eventToSendWhenStateOrTransitionChanges.m_id, m_eventToSendWhenStateOrTransitionChanges.m_payload);
                hkbEvent               event(e.getId(), e.getPayload(), context.m_character->m_behaviorGraph->getNodeTemplate(this));
                event.m_id = queue->toExternal(event.m_id);
                queue->enqueue(event);
            }
            rootBehavior->m_stateOrTransitionChanged = true;
            m_stateOrTransitionChanged               = false;
        }
        HK_TIMER_END_LIST();
    }

    int hkbStateMachine::getMaxNumChildren(GetChildrenFlags flags)
    {
        if (flags & FLAG_ACTIVE_ONLY) return 1;

        const int numStates   = m_states.getSize();
        int       numChildren = numStates;
        for (int i = 0; i < numStates; i++) {
            const TransitionInfoArray* transitions = m_states[i]->m_transitions;
            if (transitions) {
                const int numTransitions = transitions->m_transitions.getSize();
                for (int j = 0; j < numTransitions; j++) {
                    if (transitions->m_transitions[j].m_transition) numChildren++;
                }
            }
        }
        const TransitionInfoArray* wildcards = m_wildcardTransitions;
        if (wildcards) {
            const int numTransitions = wildcards->m_transitions.getSize();
            for (int j = 0; j < numTransitions; j++) {
                if (wildcards->m_transitions[j].m_transition) numChildren++;
            }
        }
        return numChildren;
    }

} }
