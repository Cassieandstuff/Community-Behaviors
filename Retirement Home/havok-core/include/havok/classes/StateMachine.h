#pragma once
#include "havok/classes/Base.h"
#include "havok/classes/Events.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// State-machine cluster, hand-ported from HKX2E Autogen. Pointer targets that
// aren't part of this batch are forward-declared (used only via shared_ptr).

namespace havok {

class hkbStateMachineEventPropertyArray;      // fwd (Arrays.h)
class hkbStateMachineTransitionInfoArray;     // fwd (Arrays.h)

// Pointer targets the trueflight subset only ever leaves null (abstract bases;
// never instantiated/serialized in our graphs). Signatures confirmed against
// HKX2E/HKX2/Autogen/hkbStateChooser.cs + hkbStateListener.cs (both 0xda8c7d7d,
// size 16 — identical empty hkReferencedObject layout). Sharing this signature
// with hkbEventPayload/hkbCondition is fine: the registry keys on class name.
class hkbStateChooser : public hkReferencedObject {
public:
    HK_CLASS_ID(0xda8c7d7du, "hkbStateChooser")
};
class hkbStateListener : public hkReferencedObject {
public:
    HK_CLASS_ID(0xda8c7d7du, "hkbStateListener")
};

// hkbStateMachineStateInfo — size 120, sig 0x0ed7f9d0. One state in an SM.
class hkbStateMachineStateInfo : public hkbBindable {
public:
    std::vector<std::shared_ptr<hkbStateListener>>      m_listeners;
    std::shared_ptr<hkbStateMachineEventPropertyArray>  m_enterNotifyEvents;
    std::shared_ptr<hkbStateMachineEventPropertyArray>  m_exitNotifyEvents;
    std::shared_ptr<hkbStateMachineTransitionInfoArray> m_transitions;
    std::shared_ptr<hkbGenerator>                       m_generator;
    std::string                                         m_name;
    std::int32_t                                        m_stateId     = 0;
    float                                               m_probability = 0.f;
    bool                                                m_enable      = false;
    HK_CLASS_ID(0x0ed7f9d0u, "hkbStateMachineStateInfo")
};

// hkbStateMachine — size 264, sig 0x816c1dcb.
class hkbStateMachine : public hkbGenerator {
public:
    hkbEvent                         m_eventToSendWhenStateOrTransitionChanges;  // inline struct
    std::shared_ptr<hkbStateChooser> m_startStateChooser;
    std::int32_t m_startStateId                       = 0;
    std::int32_t m_returnToPreviousStateEventId       = 0;
    std::int32_t m_randomTransitionEventId            = 0;
    std::int32_t m_transitionToNextHigherStateEventId = 0;
    std::int32_t m_transitionToNextLowerStateEventId  = 0;
    std::int32_t m_syncVariableIndex                  = 0;
    std::int32_t m_currentStateId                     = 0;      // SERIALIZE_IGNORED
    bool         m_wrapAroundStateId                  = false;
    std::int8_t  m_maxSimultaneousTransitions         = 0;
    std::int8_t  m_startStateMode                     = 0;      // enum StartStateMode
    std::int8_t  m_selfTransitionMode                 = 0;      // enum StateMachineSelfTransitionMode
    bool         m_isActive                           = false;  // SERIALIZE_IGNORED
    std::vector<std::shared_ptr<hkbStateMachineStateInfo>> m_states;
    std::shared_ptr<hkbStateMachineTransitionInfoArray>    m_wildcardTransitions;
    // ── SERIALIZE_IGNORED tail (typed; kept for round-trip fidelity) ──
    float         m_timeInState                 = 0.f;
    float         m_lastLocalTime               = 0.f;
    std::int32_t  m_previousStateId             = 0;
    std::int32_t  m_nextStartStateIndexOverride = 0;
    bool          m_stateOrTransitionChanged    = false;
    bool          m_echoNextUpdate              = false;
    std::uint16_t m_sCurrentStateIndexAndEntered = 0;
    HK_CLASS_ID(0x816c1dcbu, "hkbStateMachine")
};

} // namespace havok
