/// @file
/// @brief Workbench: the functions of hkbStateMachineSecondUnit.cpp still being matched (tools/cbmatch/parked.yaml says why each is
///        stuck). Compiled only by tools/cbmatch/manifests/workbench.yaml; a body that matches graduates back into
///        havok/Behavior/StateMachine/hkbStateMachineSecondUnit.cpp.
#include <Behavior/StateMachine/hkbStateMachine.h>

namespace CB { namespace havok
{
    void hkbStateMachine::getInternalState(hkReferencedObject& _internalState) const
    {
        hkbStateMachineInternalState& internalState = static_cast<hkbStateMachineInternalState&>(_internalState);

        internalState.m_activeTransitions           = m_activeTransitions;
        internalState.m_transitionFlags             = m_transitionFlags;
        internalState.m_wildcardTransitionFlags     = m_wildcardTransitionFlags;
        internalState.m_delayedTransitions          = m_delayedTransitions;
        internalState.m_timeInState                 = m_timeInState;
        internalState.m_lastLocalTime               = m_lastLocalTime;
        internalState.m_currentStateId              = m_currentStateId;
        internalState.m_previousStateId             = m_previousStateId;
        internalState.m_nextStartStateIndexOverride = m_nextStartStateIndexOverride;
        internalState.m_stateOrTransitionChanged    = m_stateOrTransitionChanged;
        internalState.m_echoNextUpdate              = m_echoNextUpdate;
    }

    void hkbStateMachine::setInternalState(const hkReferencedObject& _internalState)
    {
        const hkbStateMachineInternalState& internalState = static_cast<const hkbStateMachineInternalState&>(_internalState);

        m_activeTransitions       = internalState.m_activeTransitions;
        m_transitionFlags         = internalState.m_transitionFlags;
        m_wildcardTransitionFlags = internalState.m_wildcardTransitionFlags;
        m_delayedTransitions      = internalState.m_delayedTransitions;
        m_timeInState             = internalState.m_timeInState;
        m_lastLocalTime           = internalState.m_lastLocalTime;
        setCurrentStateId(internalState.m_currentStateId);
        m_previousStateId             = internalState.m_previousStateId;
        m_nextStartStateIndexOverride = internalState.m_nextStartStateIndexOverride;
        m_stateOrTransitionChanged    = internalState.m_stateOrTransitionChanged;
        m_echoNextUpdate              = internalState.m_echoNextUpdate;
    }

} }
