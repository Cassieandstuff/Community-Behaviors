/// @file
/// @brief hkbStateMachine (and its nested StateInfo / TransitionInfoArray), reimplemented against vanilla (AE 1.6.1170).
/// @note Vanilla's unit is StateMachine\hkbStateMachine.cpp, 0x140ad75b0–0x140adf33d.
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
    HK_FORCE_INLINE hkbStateMachine::TransitionInfo* hkbStateMachine::getTransitionInfo(hkbBehaviorGraph* rootBehavior, const TransitionInfoReference& ref)
    {
        if (ref.m_fromStateIndex == -1) {
            const hkbGlobalTransitionData* data = rootBehavior->m_globalTransitionData;
            hkbStateMachine* sm = (ref.m_stateMachineId == -1) ? this : data->m_stateMachineIdToTemplateMap.getWithDefault(ref.m_stateMachineId, HK_NULL);
            return &sm->m_wildcardTransitions->m_transitions[ref.m_transitionIndex];
        }
        return &m_states[ref.m_fromStateIndex]->m_transitions->m_transitions[ref.m_transitionIndex];
    }

    hkbStateMachine::hkbStateMachine()
    :   m_eventToSendWhenStateOrTransitionChanges(-1, HK_NULL, HK_NULL),
        m_startStateId(0),
        m_returnToPreviousStateEventId(-1),
        m_randomTransitionEventId(-1),
        m_transitionToNextHigherStateEventId(-1),
        m_transitionToNextLowerStateEventId(-1),
        m_syncVariableIndex(-1),
        m_currentStateId(0),
        m_wrapAroundStateId(true),
        m_maxSimultaneousTransitions(32),
        m_startStateMode(START_STATE_MODE_DEFAULT),
        m_selfTransitionMode(SELF_TRANSITION_MODE_NO_TRANSITION),
        m_isActive(false),
        m_wildcardTransitions(HK_NULL),
        m_stateIdToIndexMap(HK_NULL),
        m_timeInState(0.0f),
        m_lastLocalTime(0.0f),
        m_previousStateId(0),
        m_nextStartStateIndexOverride(-1),
        m_stateOrTransitionChanged(false),
        m_echoNextUpdate(false),
        m_sCurrentStateIndexAndEntered(0)
    {
        m_stateIdToIndexMap = new hkPointerMap<int, int>;
    }

    hkbStateMachine::hkbStateMachine(const hkbStateMachine& sm, hkbBehaviorGraph& rootBehavior)
    :   hkbGenerator(sm),
        m_eventToSendWhenStateOrTransitionChanges(-1, HK_NULL, HK_NULL),
        m_currentStateId(0),
        m_isActive(false),
        m_wildcardTransitions(HK_NULL),
        m_stateIdToIndexMap(HK_NULL),
        m_timeInState(0.0f),
        m_lastLocalTime(0.0f),
        m_previousStateId(0),
        m_nextStartStateIndexOverride(-1),
        m_stateOrTransitionChanged(false),
        m_echoNextUpdate(false),
        m_sCurrentStateIndexAndEntered(0)
    {
        m_eventToSendWhenStateOrTransitionChanges = sm.m_eventToSendWhenStateOrTransitionChanges;
        m_startStateChooser                       = sm.m_startStateChooser;
        hkString::memCpy(&m_startStateId, &sm.m_startStateId, 0x20);

        m_states.reserveExactly(sm.m_states.getSize());
        for (int i = 0; i < sm.m_states.getSize(); i++) {
            StateInfo*                      stateInfo    = sm.m_states[i];
            const int                       numListeners = stateInfo->m_listeners.getSize();
            hkLocalArray<hkbStateListener*> listeners(numListeners);
            listeners.setSize(numListeners);

            bool anyCloned = false;
            for (int j = 0; j < numListeners; j++) {
                hkbStateListener* clone = rootBehavior.getStateListenerClone(stateInfo->m_listeners[j]);
                listeners[j]            = clone;
                anyCloned               = anyCloned | (clone != HK_NULL);
            }

            if (stateInfo->m_variableBindingSet || anyCloned) {
                for (int j = 0; j < numListeners; j++) {
                    if (listeners[j] == HK_NULL) {
                        listeners[j] = stateInfo->m_listeners[j];
                        listeners[j]->addReference();
                    }
                }
                m_states.pushBack(new StateInfo(*stateInfo, listeners));
            } else {
                stateInfo->addReference();
                m_states.pushBack(stateInfo);
            }
        }

        m_wildcardTransitions = sm.m_wildcardTransitions;
        if (m_wildcardTransitions) m_wildcardTransitions->addReference();

        initRuntimeData();
    }

    hkbStateMachine::~hkbStateMachine()
    {
        for (int i = 0; i < m_activeTransitions.getSize(); i++) m_activeTransitions[i].m_transitionEffect->removeReference();

        if (m_states.getSize() != 0 && m_states[0] != HK_NULL) m_states[0]->removeReference();
        for (int i = 1; i < m_states.getSize(); i++) m_states[i]->removeReference();

        if (m_wildcardTransitions) m_wildcardTransitions->removeReference();
        delete m_stateIdToIndexMap;
    }

    const void* hkbStateMachine::getClassType() const
    {
        return &staticClass();
    }

    void hkbStateMachine::collectBindables(hkbBindableCollector& collector)
    {
        collector.collectBindable(this);
        const int numStates = m_states.getSize();
        for (int i = 0; i < numStates; i++) m_states[i]->collectBindables(collector);
    }

    /// @note Bethesda's build warns about transitions without an event and about trigger/initiate intervals bounded by
    ///       times rather than events.
    void hkbStateMachine::activate(const hkbContext& context)
    {
        if (m_isActive) return;

        if (m_wildcardTransitions) {
            const int numWildcards = m_wildcardTransitions->m_transitions.getSize();
            m_wildcardTransitionFlags.reserveExactly(numWildcards);
            m_wildcardTransitionFlags.setSize(numWildcards);
        }

        const int numStates      = m_states.getSize();
        int       maxTransitions = 0;
        for (int i = 0; i < numStates; i++) {
            const TransitionInfoArray* transitions    = m_states[i]->m_transitions;
            int                        numTransitions = 0;
            if (transitions) numTransitions = transitions->m_transitions.getSize();
            maxTransitions = (maxTransitions > numTransitions) ? maxTransitions : numTransitions;
            for (int j = 0; j < numTransitions; j++) {
                const StateInfo*      stateInfo = m_states[i];
                const TransitionInfo& t         = stateInfo->m_transitions->m_transitions[j];
                if (t.m_eventId == -1) {
                    const char* name = m_name.cString();
                    hkStringBuf buf;
                    buf.printf("%s: Transition from state %s to state id %d has no event set.", name, stateInfo->m_name.cString(), t.m_toStateId);
                    hkError::getInstance().message(hkError::MESSAGE_WARNING, 0xa781b89f, buf.cString(), "StateMachine\\hkbStateMachine.cpp", 0x53d);
                } else if (((t.m_flags & TransitionInfo::FLAG_USE_TRIGGER_INTERVAL) && t.m_triggerInterval.m_enterEventId == -1 &&
                            t.m_triggerInterval.m_exitEventId == -1) ||
                           ((t.m_flags & TransitionInfo::FLAG_USE_INITIATE_INTERVAL) && t.m_initiateInterval.m_enterEventId == -1 &&
                            t.m_initiateInterval.m_exitEventId == -1)) {
                    const char* name = m_name.cString();
                    hkStringBuf buf;
                    buf.printf("%s: Transition from state %s to state id %d has a fixed time transition, not supported by Bethesda Game Studios.",
                               name, stateInfo->m_name.cString(), t.m_toStateId);
                    hkError::getInstance().message(hkError::MESSAGE_WARNING, 0xa781b89f, buf.cString(), "StateMachine\\hkbStateMachine.cpp", 0x543);
                }
            }
        }
        if (maxTransitions > 0) {
            m_transitionFlags.reserveExactly(maxTransitions);
            m_transitionFlags.setSize(maxTransitions);
        }

        const int startStateId = chooseStartStateId(context);
        setCurrentState(startStateId, true, context, false);
        writeSyncVariable(context, startStateId);
        m_isActive       = true;
        m_echoNextUpdate = false;
    }

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

    void hkbStateMachine::handleEvent(const hkbContext& context, const hkbEvent e)
    {
        handleEventInternal(context, e.getId());
    }

    void hkbStateMachine::deactivate(const hkbContext& context)
    {
        if (m_isActive) {
            endTransitions(m_activeTransitions.getSize() - 1, context);
            m_delayedTransitions.clear();
            exitCurrentState(context);
            m_transitionFlags.reserveExactly(0);
            m_wildcardTransitionFlags.reserveExactly(0);
            m_isActive = false;
        }
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

    void hkbStateMachine::getChildren(GetChildrenFlags flags, ChildrenInfo& childrenInfo)
    {
        if (flags & FLAG_ACTIVE_ONLY) {
            childrenInfo.m_childInfos.setSize(1);
            childrenInfo.m_childInfos[0].m_node     = getActiveGenerator();
            childrenInfo.m_childInfos[0].m_isActive = true;
            childrenInfo.m_anyActive                = true;
            return;
        }

        int numChildren = 0;
        for (int i = 0; i < m_states.getSize(); i++) {
            childrenInfo.m_childInfos.setSize(numChildren + 1);
            childrenInfo.m_childInfos[numChildren++].m_node = m_states[i]->m_generator;
        }
        const int numStates = m_states.getSize();
        for (int i = 0; i < numStates; i++) {
            const TransitionInfoArray* transitions = m_states[i]->m_transitions;
            if (transitions) {
                const int numTransitions = transitions->m_transitions.getSize();
                for (int j = 0; j < numTransitions; j++) {
                    if (transitions->m_transitions[j].m_transition) {
                        childrenInfo.m_childInfos.setSize(numChildren + 1);
                        childrenInfo.m_childInfos[numChildren++].m_node = transitions->m_transitions[j].m_transition;
                    }
                }
            }
        }
        const TransitionInfoArray* wildcards = m_wildcardTransitions;
        if (wildcards) {
            const int numTransitions = wildcards->m_transitions.getSize();
            for (int j = 0; j < numTransitions; j++) {
                if (wildcards->m_transitions[j].m_transition) {
                    childrenInfo.m_childInfos.setSize(numChildren + 1);
                    childrenInfo.m_childInfos[numChildren++].m_node = wildcards->m_transitions[j].m_transition;
                }
            }
        }
    }

    /// @note Bethesda's build also rejects transitions with time-bounded trigger/initiate intervals and abut-at-end
    ///       transitions; every transition error is collected before the result is reported.
    bool hkbStateMachine::isValid(hkStringPtr& errorString) const
    {
        if (m_states.getSize() == 0) {
            errorString = "There must be at least one state.";
            return false;
        }
        if (m_startStateMode == START_STATE_MODE_DEFAULT && getStateIndex(m_startStateId) == -1) {
            errorString = "The start state ID is invalid.";
            return false;
        }
        if (m_startStateMode == START_STATE_MODE_CHOOSER && m_startStateChooser == HK_NULL) {
            errorString = "You must provide a start state chooser.";
            return false;
        }
        if (m_states.getSize() > 0x8000) {
            errorString = "You have too many states in this graph for a state machine optimization.";
            return false;
        }

        hkStringBuf errors;
        for (int i = 0; i < m_states.getSize(); i++) {
            const StateInfo*           stateInfo   = m_states[i];
            const TransitionInfoArray* transitions = stateInfo->m_transitions;
            if (transitions) {
                for (int j = 0; j < transitions->m_transitions.getSize(); j++) {
                    const TransitionInfo& t = transitions->m_transitions[j];
                    if (t.m_eventId == -1) {
                        hkStringBuf buf("Transition from state ");
                        buf += stateInfo->m_name.cString();
                        buf += " to state ";
                        buf += m_states[getStateIndex(t.m_toStateId)]->m_name.cString();
                        buf += " should have a valid event.\n";
                        errors += buf.cString();
                    } else if (((t.m_flags & TransitionInfo::FLAG_USE_TRIGGER_INTERVAL) && t.m_triggerInterval.m_enterEventId == -1 &&
                                t.m_triggerInterval.m_exitEventId == -1) ||
                               ((t.m_flags & TransitionInfo::FLAG_USE_INITIATE_INTERVAL) && t.m_initiateInterval.m_enterEventId == -1 &&
                                t.m_initiateInterval.m_exitEventId == -1)) {
                        hkStringBuf buf("Transition from state ");
                        buf += stateInfo->m_name.cString();
                        buf += " to state ";
                        buf += m_states[getStateIndex(t.m_toStateId)]->m_name.cString();
                        buf += " uses trigger interval times, not supported by Bethesda Game Studios.\n";
                        errors += buf.cString();
                    } else if (t.m_flags & TransitionInfo::FLAG_ABUT_AT_END_OF_FROM_GENERATOR) {
                        hkStringBuf buf("Transition from state ");
                        buf += stateInfo->m_name.cString();
                        buf += " to state ";
                        buf += m_states[getStateIndex(t.m_toStateId)]->m_name.cString();
                        buf += " uses abut at end of from generator, not supported by Bethesda Game Studios.\n";
                        errors += buf.cString();
                    }
                }
            }
        }
        const TransitionInfoArray* wildcards = m_wildcardTransitions;
        if (wildcards) {
            for (int j = 0; j < wildcards->m_transitions.getSize(); j++) {
                const TransitionInfo& t = wildcards->m_transitions[j];
                if (t.m_eventId == -1 && t.m_condition == HK_NULL) {
                    hkStringBuf buf("Wildcard transition to state ");
                    buf += m_states[getStateIndex(t.m_toStateId)]->m_name.cString();
                    buf += " should have either have a valid event or a condition.\n";
                    errors += buf.cString();
                }
            }
        }

        const bool hasErrors = errors.getLength() != 0;
        if (hasErrors) errorString = errors.cString();
        return !hasErrors;
    }

    hkbNode* hkbStateMachine::cloneNode(hkbBehaviorGraph& rootBehavior) const
    {
        return new hkbStateMachine(*this, rootBehavior);
    }

    void hkbStateMachine::getInternalStateUser(const hkbBehaviorGraph& rootBehavior, hkReferencedObject& _internalState)
    {
        hkbStateMachineInternalState& internalState  = static_cast<hkbStateMachineInternalState&>(_internalState);
        const int                     numTransitions = m_activeTransitions.getSize();
        for (int i = 0; i < numTransitions; i++) {
            hkbTransitionEffect* effect = m_activeTransitions[i].m_transitionEffect;
            if (rootBehavior.m_isActive &&
                (static_cast<const hkbNode*>(effect) == &rootBehavior || rootBehavior.m_activeNodeTemplateToIndexMap->getWithDefault(effect, -1) != -1)) {
                hkReferencedObject* effectState = effect->createInternalState();
                if (effectState) {
                    effect->getInternalState(*effectState);
                    hkbNodeInternalStateInfo* info = new hkbNodeInternalStateInfo;
                    info->m_internalState.setAndDontIncrementRefCount(effectState);
                    const hkbNode* node = effect;
                    if (effect->m_cloneState == CLONE_STATE_CLONE) node = const_cast<hkbBehaviorGraph&>(rootBehavior).getNodeTemplate(effect);
                    const hkbNodeInfo& nodeInfo = (*rootBehavior.m_activeNodes)[rootBehavior.m_activeNodeTemplateToIndexMap->getWithDefault(node, 0)];
                    hkString::memCpy(&info->m_syncInfo, &nodeInfo.m_syncInfo, sizeof(hkbGeneratorSyncInfo));
                    info->m_syncInfo.m_isMirrored   = false;
                    info->m_hasActivateBeenCalled   = nodeInfo.m_isActive;
                    info->m_name                    = effect->m_name;
                    internalState.m_activeTransitions[i].m_transitionEffectInternalStateInfo = info;
                }
            }
        }
    }

    void hkbStateMachine::setInternalStateUser(const hkbContext& context, const hkReferencedObject& _internalState,
                                               hkPointerMap<hkInt16, const hkbNodeInternalStateInfo*>& nodeIdToInternalStateMap)
    {
        const hkbStateMachineInternalState& internalState  = static_cast<const hkbStateMachineInternalState&>(_internalState);
        const int                           numTransitions = m_activeTransitions.getSize();
        for (int i = 0; i < numTransitions; i++) {
            ActiveTransitionInfo& active = m_activeTransitions[i];
            const TransitionInfo* t      = getTransitionInfo(context.m_character->m_behaviorGraph, active.m_transitionInfoReferenceForTE);
            hkbTransitionEffect*  effect = static_cast<hkbTransitionEffect*>(t->m_transition->cloneNode(*context.m_character->m_behaviorGraph));
            effect->m_id                 = context.m_character->m_behaviorGraph->getUniqueId();
            active.m_transitionEffect    = effect;
            const hkReferencedObject* savedInfo = internalState.m_activeTransitions[i].m_transitionEffectInternalStateInfo;
            if (savedInfo) {
                nodeIdToInternalStateMap.insert(effect->m_id, static_cast<const hkbNodeInternalStateInfo*>(savedInfo));
                active.m_transitionEffectInternalStateInfo = HK_NULL;
            }
            effect->setToGenerator(m_states[getStateIndex(active.m_toStateId)]->m_generator);
            hkbGenerator* fromGenerator = (i == 0) ? m_states[getStateIndex(active.m_fromStateId)]->m_generator.val() : m_activeTransitions[i - 1].m_transitionEffect;
            effect->setFromGenerator(fromGenerator);
        }
    }

    void hkbStateMachine::getActiveEvents(hkPointerMap<int, bool>& activeEvents) const
    {
        getActiveEventsOfTransitions(m_states[m_sCurrentStateIndexAndEntered >> 1]->m_transitions, activeEvents);
        getActiveEventsOfTransitions(m_wildcardTransitions, activeEvents);
        if (m_returnToPreviousStateEventId >= 0) activeEvents.insert(m_returnToPreviousStateEventId, true);
        if (m_randomTransitionEventId >= 0) activeEvents.insert(m_randomTransitionEventId, true);
        if (m_transitionToNextHigherStateEventId >= 0) activeEvents.insert(m_transitionToNextHigherStateEventId, true);
        if (m_transitionToNextLowerStateEventId >= 0) activeEvents.insert(m_transitionToNextLowerStateEventId, true);
    }

    void hkbStateMachine::getActiveVariables(hkPointerMap<int, bool>& activeVariables) const
    {
        if (m_syncVariableIndex >= 0) activeVariables.insert(m_syncVariableIndex, true);
    }

    void hkbStateMachine::generate(const hkbContext& context, const hkbGeneratorOutput** activeChildrenOutput, hkbGeneratorOutput& output,
                                   hkReal timeOffset)
    {
        if (m_isActive && &output != activeChildrenOutput[0]) hkbGeneratorOutputUtils::copyGeneratorOutput(*activeChildrenOutput[0], output);
    }

    bool hkbStateMachine::canRecycleOutput() const
    {
        return true;
    }

    void hkbStateMachine::updateSync(const hkbContext& context, hkbNodeInfo& info)
    {
        hkbBehaviorGraph* rootBehavior = context.m_character->m_behaviorGraph;
        const int         childIndex   = (*rootBehavior->m_activeNodesChildrenIndices)[info.m_childrenIndicesStart];
        info.m_isProxy                 = true;
        info.m_proxyIndex =
            (*rootBehavior->m_activeNodes)[childIndex].m_isProxy ? (*rootBehavior->m_activeNodes)[childIndex].m_proxyIndex : childIndex;
    }

    void hkbStateMachine::startEcho(hkReal echoDuration)
    {
        m_echoNextUpdate = true;
    }

    hkbStateMachine::ActiveTransitionInfo::~ActiveTransitionInfo()
    {
        if (m_transitionEffectInternalStateInfo) m_transitionEffectInternalStateInfo->removeReference();
    }

    hkbStateMachine::TransitionInfoArray::~TransitionInfoArray()
    {
        const int numTransitions = m_transitions.getSize();
        for (int i = 0; i < numTransitions; i++) {
            TransitionInfo& t = m_transitions[i];
            if (t.m_transition) t.m_transition->removeReference();
            if (t.m_condition) t.m_condition->removeReference();
        }
    }

    hkbStateMachine::StateInfo::StateInfo(const StateInfo& stateInfo, const hkArray<hkbStateListener*>& listeners)
    :   hkbBindable(stateInfo),
        m_generator(stateInfo.m_generator),
        m_name(stateInfo.m_name),
        m_stateId(stateInfo.m_stateId),
        m_probability(stateInfo.m_probability),
        m_enable(stateInfo.m_enable)
    {
        m_transitions       = stateInfo.m_transitions;
        m_enterNotifyEvents = stateInfo.m_enterNotifyEvents;
        m_exitNotifyEvents  = stateInfo.m_exitNotifyEvents;
        m_listeners         = listeners;
    }

    hkbStateMachine::StateInfo::~StateInfo()
    {
        for (int i = 0; i < m_listeners.getSize(); i++) m_listeners[i]->removeReference();
    }

    void hkbStateMachine::StateInfo::setGenerator(hkbGenerator* generator)
    {
        m_generator = generator;
    }

    void hkbStateMachine::StateInfo::addListener(hkbStateListener* listener)
    {
        listener->addReference();
        m_listeners.pushBack(listener);
    }

    void hkbStateMachine::StateInfo::addEnterNotifyEvent(int id, hkbEventPayload* payload)
    {
        if (!m_enterNotifyEvents) m_enterNotifyEvents.setAndDontIncrementRefCount(new EventPropertyArray);
        const hkbEventProperty e(id, payload);
        m_enterNotifyEvents->m_events.pushBack(e);
    }

    void hkbStateMachine::StateInfo::addExitNotifyEvent(int id, hkbEventPayload* payload)
    {
        if (!m_exitNotifyEvents) m_exitNotifyEvents.setAndDontIncrementRefCount(new EventPropertyArray);
        const hkbEventProperty e(id, payload);
        m_exitNotifyEvents->m_events.pushBack(e);
    }

    void hkbStateMachine::StateInfo::updateListeners(const hkbContext& context, hkReal timestep, hkbStateMachine* sm)
    {
        for (int i = 0; i < m_listeners.getSize(); i++) m_listeners[i]->onUpdate(context, timestep, sm);
    }

    void hkbStateMachine::StateInfo::enter(const hkbContext& context, hkbStateMachine* sm)
    {
        const EventPropertyArray* events = m_enterNotifyEvents;
        if (events) {
            const int numEvents = events->m_events.getSize();
            for (int i = 0; i < numEvents; i++) {
                const hkbEventProperty& e = events->m_events[i];
                if (e.getId() != -1) {
                    hkbBehaviorGraph* behavior = context.m_character->m_behaviorGraph;
                    hkbEventQueue*    queue    = context.getEventQueue();
                    hkbNode*          sender   = behavior->getNodeTemplate(sm);
                    hkbEvent          event(e.getId(), e.getPayload(), sender);
                    event.m_id = queue->toExternal(event.m_id);
                    queue->enqueue(event);
                }
            }
        }
        for (int i = 0; i < m_listeners.getSize(); i++) m_listeners[i]->onEnter(context, sm);
    }

    void hkbStateMachine::StateInfo::exit(const hkbContext& context, hkbStateMachine* sm)
    {
        const EventPropertyArray* events = m_exitNotifyEvents;
        if (events) {
            const int numEvents = events->m_events.getSize();
            for (int i = 0; i < numEvents; i++) {
                const hkbEventProperty& e = events->m_events[i];
                if (e.getId() != -1) {
                    hkbBehaviorGraph* behavior = context.m_character->m_behaviorGraph;
                    hkbEventQueue*    queue    = context.getEventQueue();
                    hkbNode*          sender   = behavior->getNodeTemplate(sm);
                    hkbEvent          event(e.getId(), e.getPayload(), sender);
                    event.m_id = queue->toExternal(event.m_id);
                    queue->enqueue(event);
                }
            }
        }
        for (int i = 0; i < m_listeners.getSize(); i++) m_listeners[i]->onExit(context, sm);
    }

    void hkbStateMachine::addState(int stateId, hkbGenerator* generator, const char* name)
    {
        if (stateId == 0) {
            const int numStates = m_states.getSize();
            if (numStates != 0) {
                StateInfo* first = m_states[0];
                m_states.pushBack(first);
                m_stateIdToIndexMap->insert(first->m_stateId, numStates);
            } else {
                m_states.pushBack(HK_NULL);
            }
            m_states[0] = new StateInfo(0, generator, name);
        } else {
            const int numStates = m_states.getSize();
            m_states.pushBack(new StateInfo(stateId, generator, name));
            m_stateIdToIndexMap->insert(stateId, numStates);
        }
    }

    int hkbStateMachine::getStateIndex(int stateId) const
    {
        if (stateId == 0) {
            if (m_states.getSize() != 0 && m_states[0]->m_stateId == 0) return 0;
            return -1;
        }
        return m_stateIdToIndexMap->getWithDefault(stateId, -1);
    }

    int hkbStateMachine::getFirstFreeStateId() const
    {
        int stateId = 0;
        while (getStateIndex(stateId) != -1) stateId++;
        return stateId;
    }

    hkbGenerator* hkbStateMachine::getActiveGenerator() const
    {
        if (m_activeTransitions.getSize() > 0) return m_activeTransitions[m_activeTransitions.getSize() - 1].m_transitionEffect;
        return m_states[m_sCurrentStateIndexAndEntered >> 1]->m_generator;
    }

    bool hkbStateMachine::isFromGeneratorAtEnd(hkbBehaviorGraph* rootBehavior, hkReal timestep) const
    {
        return false;
    }

    void hkbStateMachine::removeTransitionsToState(int toStateId)
    {
        for (int i = 0; i < m_states.getSize(); i++) removeTransitions(toStateId, m_states[i]->m_transitions);
        if (m_wildcardTransitions) removeTransitions(toStateId, m_wildcardTransitions);
    }

    void hkbStateMachine::removeTransitions(int toStateId, TransitionInfoArray* transitions)
    {
        if (transitions) {
            int i = 0;
            while (i < transitions->m_transitions.getSize()) {
                TransitionInfo& t = transitions->m_transitions[i];
                if (t.m_toStateId == toStateId) {
                    if (t.m_transition) t.m_transition->removeReference();
                    if (t.m_condition) t.m_condition->removeReference();
                    transitions->m_transitions.removeAt(i);
                } else {
                    i++;
                }
            }
        }
    }

    void hkbStateMachine::getActiveEventsOfTransitions(const TransitionInfoArray* transitions, hkPointerMap<int, bool>& activeEvents)
    {
        if (!transitions) return;
        const int numTransitions = transitions->m_transitions.getSize();
        for (int i = 0; i < numTransitions; i++) {
            const TransitionInfo& t = transitions->m_transitions[i];
            if (t.m_eventId >= 0) activeEvents.insert(t.m_eventId, true);
            const hkInt16 flags = t.m_flags;
            if (flags & TransitionInfo::FLAG_USE_TRIGGER_INTERVAL) {
                if (t.m_triggerInterval.m_enterEventId >= 0) activeEvents.insert(t.m_triggerInterval.m_enterEventId, true);
                if (t.m_triggerInterval.m_exitEventId >= 0) activeEvents.insert(t.m_triggerInterval.m_exitEventId, true);
            }
            if (flags & TransitionInfo::FLAG_USE_INITIATE_INTERVAL) {
                if (t.m_initiateInterval.m_enterEventId >= 0) activeEvents.insert(t.m_initiateInterval.m_enterEventId, true);
                if (t.m_initiateInterval.m_exitEventId >= 0) activeEvents.insert(t.m_initiateInterval.m_exitEventId, true);
            }
        }
    }

    void hkbStateMachine::calcContentStatistics(hkStatisticsCollector* collector, const hkClass* cls) const
    {
        hkStatisticsCollectorUtil::addDeclaredMembers(this, &hkbStateMachineClass, collector);
        collector->addArray("activeTransitionsInfos", m_activeTransitions, hkStatisticsCollector::MEMORY_RUNTIME);
        for (int i = 0; i < m_activeTransitions.getSize(); i++) {
            collector->addNormalChunk("activeTransitionInfo", &m_activeTransitions[i], sizeof(ActiveTransitionInfo));
        }
        const hkPointerMapStorage& map = m_stateIdToIndexMap->m_map;
        const int                  size          = map.getMemSize();
        const int                  allocatedSize = hkStatisticsCollector::getAllocatedSize(map.m_elem, size);
        collector->addChunk(hkStatisticsCollector::MEMORY_NORMAL, "stateIdToIndexMap", map.m_elem, size, allocatedSize);
        hkbGenerator::calcContentStatistics(collector, &hkbGeneratorClass);
    }

    void hkbStateMachine::removeState(int stateId)
    {
        const int index = getStateIndex(stateId);
        if (index != -1) {
            removeTransitionsToState(stateId);
            m_states[index]->removeReference();
            m_states.removeAt(index);
            if (stateId != 0) m_stateIdToIndexMap->remove(m_stateIdToIndexMap->findKey(stateId));
            if (index < m_states.getSize()) m_stateIdToIndexMap->insert(m_states[index]->m_stateId, index);
        }
    }

    void hkbStateMachine::transitionToState(int toStateId, const hkbContext& context, bool immediate, bool forceIfNoTransition,
                                            bool isReturnToPreviousState)
    {
        hkbBehaviorGraph* rootBehavior = context.m_character->m_behaviorGraph;
        if (m_isActive && !immediate) {
            if (isInterruptible(rootBehavior)) {
                hkArray<ProspectiveTransitionInfo> transitions;
                findTransitions(rootBehavior, -1, m_currentStateId, toStateId, true, false, context.m_behavior->m_eventIDMap, context, transitions);
                if (transitions.getSize() > 0) {
                    beginTransitions(context, transitions, -1, true, isReturnToPreviousState);
                    return;
                }
            }
            if (!forceIfNoTransition) return;
        }
        endTransitions(m_activeTransitions.getSize() - 1, context);
        m_delayedTransitions.clear();
        m_stateOrTransitionChanged        = true;
        rootBehavior->m_updateActiveNodes = rootBehavior->m_updateActiveNodes | true;
        context.m_flags                   = 1;
        setCurrentState(toStateId, isReturnToPreviousState, context, false);
        writeSyncVariable(context, toStateId);
    }

    void hkbStateMachine::swapStateIds(int oldStateId, int newStateId)
    {
        if (oldStateId == newStateId) return;

        int oldIndex = 0;
        int newIndex = 0;
        if (oldStateId == 0 || (oldIndex = m_stateIdToIndexMap->getWithDefault(oldStateId, -1)) != -1) {
            if (newStateId != 0) newIndex = m_stateIdToIndexMap->getWithDefault(newStateId, -1);
            if (oldStateId != 0) m_stateIdToIndexMap->remove(oldStateId);
            if (newStateId != 0) m_stateIdToIndexMap->remove(newStateId);

            TransitionInfoArray* wildcards = m_wildcardTransitions;
            if (wildcards) {
                const int numTransitions = wildcards->m_transitions.getSize();
                for (int j = 0; j < numTransitions; j++) {
                    hkInt32& toStateId = wildcards->m_transitions[j].m_toStateId;
                    if (toStateId == oldStateId)      toStateId = newStateId;
                    else if (toStateId == newStateId) toStateId = oldStateId;
                }
            }
            const int numStates = m_states.getSize();
            for (int i = 0; i < numStates; i++) {
                StateInfo* stateInfo      = m_states[i];
                const int  numTransitions = stateInfo->m_transitions ? stateInfo->m_transitions->m_transitions.getSize() : 0;
                for (int j = 0; j < numTransitions; j++) {
                    hkInt32& toStateId = stateInfo->m_transitions->m_transitions[j].m_toStateId;
                    if (toStateId == oldStateId)      toStateId = newStateId;
                    else if (toStateId == newStateId) toStateId = oldStateId;
                }
            }

            StateInfo* oldState = m_states[oldIndex];
            oldState->m_stateId = newStateId;
            StateInfo* newState = HK_NULL;
            if (newIndex != -1) {
                newState            = m_states[newIndex];
                newState->m_stateId = oldStateId;
            }
            int movedIndex = oldIndex;
            if ((newStateId == 0 || oldStateId == 0) && newState) {
                m_states[newIndex] = oldState;
                m_states[oldIndex] = newState;
                movedIndex         = newIndex;
                newIndex           = oldIndex;
            }
            if (newStateId != 0) m_stateIdToIndexMap->insert(newStateId, movedIndex);
            if (oldStateId != 0 && newState) m_stateIdToIndexMap->insert(oldStateId, newIndex);
        }
    }

    void hkbStateMachine::beginTransitionWithBestEffect(const hkbContext& context, ProspectiveTransitionInfo& transition)
    {
        hkArray<TransitionInfoReference> fromStateTransitions;
        getTransitions(HK_NULL, m_sCurrentStateIndexAndEntered >> 1, -1, transition.m_toStateId, true, false, false, context, fromStateTransitions);
        hkArray<TransitionInfoReference> wildcardTransitions;
        getTransitions(HK_NULL, -1, -1, transition.m_toStateId, true, false, true, context, wildcardTransitions);

        hkbBehaviorGraph*     rootBehavior  = context.m_character->m_behaviorGraph;
        const TransitionInfo* fromStateBest = (fromStateTransitions.getSize() != 0) ? getTransitionInfo(rootBehavior, fromStateTransitions[0]) : HK_NULL;
        const TransitionInfo* wildcardBest  = (wildcardTransitions.getSize() != 0) ? getTransitionInfo(rootBehavior, wildcardTransitions[0]) : HK_NULL;
        if (wildcardBest && (!fromStateBest || wildcardBest->m_priority > fromStateBest->m_priority)) {
            if (wildcardBest->m_priority > getTransitionInfo(rootBehavior, transition.m_transitionInfoReference)->m_priority) {
                transition.m_transitionInfoReferenceForTE = wildcardTransitions[0];
            }
        } else if (fromStateBest) {
            if (fromStateBest->m_priority > getTransitionInfo(rootBehavior, transition.m_transitionInfoReference)->m_priority) {
                transition.m_transitionInfoReferenceForTE = fromStateTransitions[0];
            }
        }
        beginTransition(context, transition, false);
    }

    void hkbStateMachine::processDelayedTransitions(const hkbContext& context, bool force)
    {
        if (m_echoNextUpdate) {
            const int startStateId = chooseStartStateId(context);
            if (startStateId != m_currentStateId && m_selfTransitionMode != SELF_TRANSITION_MODE_NO_TRANSITION) {
                transitionToState(startStateId, context, false, m_selfTransitionMode == SELF_TRANSITION_MODE_FORCE_TRANSITION_TO_START_STATE, false);
            }
            m_echoNextUpdate = false;
        }

        const int              numDelayed = m_delayedTransitions.getSize();
        DelayedTransitionInfo* delayed    = m_delayedTransitions.begin();
        for (int i = 0; i < numDelayed; i++) {
            const TransitionInfoReference& ref   = delayed[i].m_delayedTransition.m_transitionInfoReference;
            const hkUint8*                 flags = (ref.m_fromStateIndex == -1) ? m_wildcardTransitionFlags.begin() : m_transitionFlags.begin();
            if (force || (flags[ref.m_transitionIndex] & TransitionInfo::FLAG_INTERNAL_IN_INITIATE_INTERVAL)) {
                beginTransition(context, delayed[i].m_delayedTransition, delayed[i].m_isDelayedTransitionReturnToPreviousState);
                m_delayedTransitions.clear();
                return;
            }
        }
    }

    void hkbStateMachine::updateActiveTransitions(const hkbContext& context)
    {
        const int         lastIndex    = m_activeTransitions.getSize() - 1;
        hkbBehaviorGraph* rootBehavior = context.m_character->m_behaviorGraph;
        for (int i = lastIndex; i >= 0; i--) {
            bool                 notAChild = false;
            hkbTransitionEffect* effect    = m_activeTransitions[i].m_transitionEffect;
            if (!rootBehavior->m_isActive ||
                (static_cast<hkbNode*>(effect) != rootBehavior && rootBehavior->m_activeNodeTemplateToIndexMap->getWithDefault(effect, -1) == -1) || effect->isDone()) {
                endTransitions(i, context);
                return;
            }
            if (i < lastIndex) {
                {
                    hkbGenerator*           newer = m_activeTransitions[i + 1].m_transitionEffect;
                    hkLocalArray<ChildInfo> childInfos(newer->getMaxNumChildren(FLAG_ACTIVE_ONLY));
                    ChildrenInfo            childrenInfo(childInfos);
                    newer->getChildren(FLAG_ACTIVE_ONLY, childrenInfo);
                    const int numChildren = childInfos.getSize();
                    int       j           = 0;
                    for (; j < numChildren; j++) {
                        if (childInfos[j].m_node == effect) break;
                    }
                    if (j == numChildren) notAChild = true;
                }
                if (notAChild) {
                    endTransitions(i, context);
                    return;
                }
            }
        }
    }
} }
