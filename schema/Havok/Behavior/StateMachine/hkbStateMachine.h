/// @file
/// @brief hkbStateMachine: a generator that plays one of its states at a time, transitioning between them.
#pragma once

#include <Behavior/Generator/hkbGenerator.h>

namespace CB { namespace schema
{
    /// @note Declared only as far as the reimplemented code reads it (offsets per the class's reflection, AE 1.6.1170);
    ///       not constructible yet.
    class hkbStateMachine : public hkbGenerator
    {
    public:
        /// @brief A transition in progress.
        /// @note 0x28 bytes; only m_toStateId is named.
        struct ActiveTransitionInfo
        {
            hkUint8 m_opaque[0x20];
            hkInt32 m_toStateId;   ///< +0x20
            hkUint8 m_pad24[4];
        };

        hkbEvent                      m_eventToSendWhenStateOrTransitionChanges;   ///< +0x48
        void*                         m_startStateChooser;                         ///< +0x60
        hkInt32                       m_startStateId;                              ///< +0x68
        hkInt32                       m_returnToPreviousStateEventId;              ///< +0x6c
        hkInt32                       m_randomTransitionEventId;                   ///< +0x70
        hkInt32                       m_transitionToNextHigherStateEventId;        ///< +0x74
        hkInt32                       m_transitionToNextLowerStateEventId;         ///< +0x78
        hkInt32                       m_syncVariableIndex;                         ///< +0x7c
        hkInt32                       m_currentStateId;                            ///< +0x80
        hkUint8                       m_pad84[0x90 - 0x84];
        hkArray<void*>                m_states;                                    ///< +0x90 — hkbStateMachineStateInfo*
        void*                         m_wildcardTransitions;                       ///< +0xa0
        void*                         m_stateIdToIndexMap;                         ///< +0xa8
        hkArray<ActiveTransitionInfo> m_activeTransitions;                         ///< +0xb0
        hkUint8                       m_padC0[0x108 - 0xc0];

        static const hkClass& staticClass();

        /// @brief The index in m_states of the state `stateId`, -1 for none.
        /// @note Out of line in vanilla (0x140ada660).
        int getStateIndex(int stateId) const;
    };
    static_assert(sizeof(hkbStateMachine) == 0x108, "hkbStateMachine size");
} }
