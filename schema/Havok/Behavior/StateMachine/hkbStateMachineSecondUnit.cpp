/// @file
/// @brief hkbStateMachine's internal-state accessors (vtable slots 13, 14, 16), compiled apart from
///        StateMachine\hkbStateMachine.cpp.
/// @note Not a Havok file name. Vanilla keeps these at 0x140b038e0–0x140b0406f, right after hkbClipGenerator's second
///       unit (hkbClipGeneratorSecondUnit.cpp), far from the class's own unit; they may well share that unit.
#include <Behavior/StateMachine/hkbStateMachine.h>

namespace CB { namespace schema
{
    hkReferencedObject* hkbStateMachine::createInternalState()
    {
        return new hkbStateMachineInternalState;
    }

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
