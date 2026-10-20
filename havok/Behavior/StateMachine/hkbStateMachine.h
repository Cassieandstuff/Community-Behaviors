/// @file
/// @brief hkbStateMachine: a generator that plays one of its states at a time, transitioning between them.
#pragma once

#include <Behavior/Generator/hkbGenerator.h>
#include <Behavior/StateMachine/hkbStateListener.h>

namespace CB { namespace havok
{
    extern const hkClass hkbStateMachineClass;
    class hkbTransitionEffect;
    class hkbCondition;
    class hkbStateChooser;
    struct hkbGeneratorOutput;
    struct hkbSymbolIdMap;

    /// @note Offsets per the class's reflection (AE 1.6.1170). The nested types carry the names Havok's reflection gives
    ///       them (`hkbStateMachine::TransitionInfo` …, strings in the exe).
    class hkbStateMachine : public hkbGenerator
    {
    public:
        enum StartStateMode { START_STATE_MODE_DEFAULT = 0, START_STATE_MODE_SYNC = 1, START_STATE_MODE_RANDOM = 2, START_STATE_MODE_CHOOSER = 3 };
        enum StateMachineSelfTransitionMode
        {
            SELF_TRANSITION_MODE_NO_TRANSITION                   = 0,
            SELF_TRANSITION_MODE_TRANSITION_TO_START_STATE       = 1,
            SELF_TRANSITION_MODE_FORCE_TRANSITION_TO_START_STATE = 2,
        };

        /// @brief An interval of the from-state's playback, bounded by events or by times.
        struct TimeInterval
        {
            hkInt32 m_enterEventId;   ///< +0x00
            hkInt32 m_exitEventId;    ///< +0x04
            hkReal  m_enterTime;      ///< +0x08
            hkReal  m_exitTime;       ///< +0x0c
        };

        /// @note Plain old data: the transition and condition are raw references, released by the owning
        ///       TransitionInfoArray (first to last) or when a transition is removed.
        struct TransitionInfo
        {
            enum TransitionFlags
            {
                FLAG_USE_TRIGGER_INTERVAL              = 0x1,
                FLAG_USE_INITIATE_INTERVAL             = 0x2,
                FLAG_UNINTERRUPTIBLE_WHILE_PLAYING     = 0x4,
                FLAG_UNINTERRUPTIBLE_WHILE_DELAYED     = 0x8,
                FLAG_DELAY_STATE_CHANGE                = 0x10,
                FLAG_DISABLED                          = 0x20,
                FLAG_DISALLOW_RETURN_TO_PREVIOUS_STATE = 0x40,
                FLAG_DISALLOW_RANDOM_TRANSITION        = 0x80,
                FLAG_DISABLE_CONDITION                 = 0x100,
                FLAG_ALLOW_SELF_TRANSITION_BY_TRANSITION_FROM_ANY_STATE = 0x200,
                FLAG_IS_GLOBAL_WILDCARD                = 0x400,
                FLAG_IS_LOCAL_WILDCARD                 = 0x800,
                FLAG_FROM_NESTED_STATE_ID_IS_VALID     = 0x1000,
                FLAG_TO_NESTED_STATE_ID_IS_VALID       = 0x2000,
                FLAG_ABUT_AT_END_OF_FROM_GENERATOR     = 0x4000,
            };
            /// @brief The bits of hkbStateMachine::m_transitionFlags / m_wildcardTransitionFlags.
            enum InternalFlagBits { FLAG_INTERNAL_IN_TRIGGER_INTERVAL = 0x1, FLAG_INTERNAL_IN_INITIATE_INTERVAL = 0x2 };

            TimeInterval         m_triggerInterval;     ///< +0x00
            TimeInterval         m_initiateInterval;    ///< +0x10
            hkbTransitionEffect* m_transition;          ///< +0x20
            hkbCondition*        m_condition;           ///< +0x28
            hkInt32              m_eventId;             ///< +0x30
            hkInt32              m_toStateId;           ///< +0x34
            hkInt32              m_fromNestedStateId;   ///< +0x38
            hkInt32              m_toNestedStateId;     ///< +0x3c
            hkInt16              m_priority;            ///< +0x40
            hkInt16              m_flags;               ///< +0x42
            hkUint8              m_pad44[4];
        };

        /// @brief Where a transition lives: m_states[m_fromStateIndex]'s transitions, or (m_fromStateIndex -1) the
        ///        wildcard transitions of this machine, or of the template state machine m_stateMachineId names.
        struct TransitionInfoReference
        {
            hkInt16 m_fromStateIndex;    ///< +0x00
            hkInt16 m_transitionIndex;   ///< +0x02
            hkInt16 m_stateMachineId;    ///< +0x04

            /// @note -2 marks a reference not set yet.
            HK_FORCE_INLINE TransitionInfoReference() : m_fromStateIndex(-2), m_transitionIndex(-1), m_stateMachineId(-1) {}
        };

        /// @brief A transition in progress.
        struct ActiveTransitionInfo
        {
            hkbTransitionEffect*    m_transitionEffect;                     ///< +0x00 — a clone this machine holds a reference on
            /// @note Released (never nulled) by the destructor; hkbNodeInternalStateInfo in the Havok SDK.
            hkReferencedObject*     m_transitionEffectInternalStateInfo;    ///< +0x08
            TransitionInfoReference m_transitionInfoReference;              ///< +0x10
            TransitionInfoReference m_transitionInfoReferenceForTE;         ///< +0x16
            hkInt32                 m_fromStateId;                          ///< +0x1c
            hkInt32                 m_toStateId;                            ///< +0x20
            hkBool                  m_isReturnToPreviousState;              ///< +0x24
            hkUint8                 m_pad25[3];

            HK_FORCE_INLINE ActiveTransitionInfo() : m_transitionEffectInternalStateInfo(HK_NULL) {}
            /// @note Out of line in vanilla (0x140ad93d0).
            ~ActiveTransitionInfo();
        };

        /// @brief A transition that has been chosen but not started.
        struct ProspectiveTransitionInfo
        {
            TransitionInfoReference m_transitionInfoReference;        ///< +0x00
            TransitionInfoReference m_transitionInfoReferenceForTE;   ///< +0x06
            hkInt32                 m_toStateId;                      ///< +0x0c
        };

        /// @brief A transition waiting for its from-generator (FLAG_DELAY_STATE_CHANGE).
        struct DelayedTransitionInfo
        {
            ProspectiveTransitionInfo m_delayedTransition;                         ///< +0x00
            hkReal                    m_timeDelayed;                               ///< +0x10
            hkBool                    m_isDelayedTransitionReturnToPreviousState;  ///< +0x14
            hkBool                    m_wasInAbutRangeLastFrame;                   ///< +0x15
            hkUint8                   m_pad16[2];
        };

        class TransitionInfoArray : public hkReferencedObject
        {
        public:
            hkArray<TransitionInfo> m_transitions;   ///< +0x10

            /// @brief Releases each transition's effect and condition.
            ~TransitionInfoArray();
        };

        class EventPropertyArray : public hkReferencedObject
        {
        public:
            hkArray<hkbEventProperty> m_events;   ///< +0x10
        };

        class StateInfo : public hkbBindable
        {
        public:
            /// @note Each listener holds a reference (raw pointers: released one by one by the destructor).
            hkArray<hkbStateListener*>   m_listeners;           ///< +0x30
            hkRefPtr<EventPropertyArray> m_enterNotifyEvents;   ///< +0x40
            hkRefPtr<EventPropertyArray> m_exitNotifyEvents;    ///< +0x48
            hkRefPtr<TransitionInfoArray> m_transitions;        ///< +0x50
            hkRefPtr<hkbGenerator>       m_generator;           ///< +0x58
            hkStringPtr                  m_name;                ///< +0x60
            hkInt32                      m_stateId;             ///< +0x68
            hkReal                       m_probability;         ///< +0x6c
            hkBool                       m_enable;              ///< +0x70

            HK_FORCE_INLINE StateInfo(int stateId, hkbGenerator* generator, const char* name)
            :   m_generator(generator), m_name(name), m_stateId(stateId), m_probability(1.0f), m_enable(true) {}
            /// @brief A copy of `stateInfo` that takes `listeners` (a clone's listeners) in place of its own.
            StateInfo(const StateInfo& stateInfo, const hkArray<hkbStateListener*>& listeners);
            ~StateInfo();

            void setGenerator(hkbGenerator* generator);
            /// @pre `listener` is non-null.
            void addListener(hkbStateListener* listener);
            void addEnterNotifyEvent(int id, hkbEventPayload* payload);
            void addExitNotifyEvent(int id, hkbEventPayload* payload);

            /// @brief Calls each listener's onUpdate.
            /// @note Name provisional (vanilla's monitor timer around the call: "StUpdateStateListeners").
            void updateListeners(const hkbContext& context, hkReal timestep, hkbStateMachine* sm);
            /// @brief Raises the enter notify events (sent by the template of `sm`), then calls each listener's onEnter.
            /// @note Name provisional.
            void enter(const hkbContext& context, hkbStateMachine* sm);
            /// @brief Raises the exit notify events, then calls each listener's onExit.
            /// @note Name provisional.
            void exit(const hkbContext& context, hkbStateMachine* sm);
        };

        hkbEvent                                    m_eventToSendWhenStateOrTransitionChanges;   ///< +0x48
        hkRefPtr<hkbStateChooser>                   m_startStateChooser;                         ///< +0x60
        hkInt32                                     m_startStateId;                              ///< +0x68
        hkInt32                                     m_returnToPreviousStateEventId;              ///< +0x6c
        hkInt32                                     m_randomTransitionEventId;                   ///< +0x70
        hkInt32                                     m_transitionToNextHigherStateEventId;        ///< +0x74
        hkInt32                                     m_transitionToNextLowerStateEventId;         ///< +0x78
        hkInt32                                     m_syncVariableIndex;                         ///< +0x7c
        hkInt32                                     m_currentStateId;                            ///< +0x80
        hkBool                                      m_wrapAroundStateId;                         ///< +0x84
        hkInt8                                      m_maxSimultaneousTransitions;                ///< +0x85
        hkInt8                                      m_startStateMode;                            ///< +0x86
        hkInt8                                      m_selfTransitionMode;                        ///< +0x87
        hkBool                                      m_isActive;                                  ///< +0x88
        hkUint8                                     m_pad89[7];
        /// @note Each state holds a reference (raw pointers).
        hkArray<StateInfo*>                         m_states;                                    ///< +0x90
        /// @note A raw reference: shared (not copied) by clones, released in the destructor's body.
        TransitionInfoArray*                        m_wildcardTransitions;                       ///< +0xa0
        hkPointerMap<int, int>*                     m_stateIdToIndexMap;                         ///< +0xa8
        hkArray<ActiveTransitionInfo>               m_activeTransitions;                         ///< +0xb0
        /// @brief Per transition of the current state: TransitionInfo internal flag bits (0x1 in trigger interval,
        ///        0x2 in initiate interval).
        hkArray<hkUint8>                            m_transitionFlags;                           ///< +0xc0
        hkArray<hkUint8>                            m_wildcardTransitionFlags;                   ///< +0xd0
        hkArray<DelayedTransitionInfo>              m_delayedTransitions;                        ///< +0xe0
        hkReal                                      m_timeInState;                               ///< +0xf0
        hkReal                                      m_lastLocalTime;                             ///< +0xf4
        hkInt32                                     m_previousStateId;                           ///< +0xf8
        hkInt32                                     m_nextStartStateIndexOverride;               ///< +0xfc
        hkBool                                      m_stateOrTransitionChanged;                  ///< +0x100
        hkBool                                      m_echoNextUpdate;                            ///< +0x101
        /// @brief The current state's index in m_states, shifted left once; bit 0 set once the state has been entered.
        hkUint16                                    m_sCurrentStateIndexAndEntered;              ///< +0x102
        hkUint8                                     m_pad104[4];

        hkbStateMachine();
        /// @brief The cloneNode copy: a state with bindings or with a listener that clones is copied (taking the root
        ///        behavior's listener clones, sharing the rest), the others are shared; so are the wildcard transitions.
        hkbStateMachine(const hkbStateMachine& sm, hkbBehaviorGraph& rootBehavior);
        hkbStateMachine(hkFinishLoadedObjectFlag flag);
        virtual ~hkbStateMachine();   ///< slot 0

        static const hkClass& staticClass();

        virtual const void* getClassType() const;                                                    ///< slot 1
        virtual void        calcContentStatistics(hkStatisticsCollector* collector, const hkClass* cls) const;   ///< slot 2
        virtual void        collectBindables(hkbBindableCollector& collector);                       ///< slot 3
        virtual void        activate(const hkbContext& context);                                     ///< slot 4
        virtual void        update(const hkbContext& context, hkReal timestep);                      ///< slot 5
        virtual void        handleEvent(const hkbContext& context, const hkbEvent e);                ///< slot 6
        virtual void        deactivate(const hkbContext& context);                                   ///< slot 7
        /// @brief One under FLAG_ACTIVE_ONLY; else every state's generator plus every transition effect.
        virtual int         getMaxNumChildren(GetChildrenFlags flags);                               ///< slot 8
        virtual void        getChildren(GetChildrenFlags flags, ChildrenInfo& childrenInfo);         ///< slot 9
        virtual bool        isValid(hkStringPtr& errorString) const;                                 ///< slot 10
        virtual hkbNode*    cloneNode(hkbBehaviorGraph& rootBehavior) const;                         ///< slot 12
        virtual hkReferencedObject* createInternalState();                                           ///< slot 13
        virtual void        getInternalState(hkReferencedObject& internalState) const;               ///< slot 14
        /// @brief Saves an internal-state record for each active transition's effect that is active in `rootBehavior`.
        virtual void        getInternalStateUser(const hkbBehaviorGraph& rootBehavior, hkReferencedObject& internalState);   ///< slot 15
        virtual void        setInternalState(const hkReferencedObject& internalState);               ///< slot 16
        /// @brief Re-clones the active transitions' effects (setInternalState restored their records), hands their saved
        ///        records to `nodeIdToInternalStateMap` under the new ids, and reconnects their from/to generators.
        virtual void        setInternalStateUser(const hkbContext& context, const hkReferencedObject& internalState,
                                             hkPointerMap<hkInt16, const hkbNodeInternalStateInfo*>& nodeIdToInternalStateMap);   ///< slot 17
        virtual void        getActiveEvents(hkPointerMap<int, bool>& activeEvents) const;            ///< slot 18
        /// @brief Adds the sync variable, when there is one.
        virtual void        getActiveVariables(hkPointerMap<int, bool>& activeVariables) const;   ///< slot 20
        /// @brief While active, copies the active child's output into `output`, unless they are the same buffer.
        virtual void        generate(const hkbContext& context, const hkbGeneratorOutput** activeChildrenOutput, hkbGeneratorOutput& output,
                                     hkReal timeOffset);                                             ///< slot 23
        virtual bool        canRecycleOutput() const;                                                ///< slot 24
        virtual void        updateSync(const hkbContext& context, hkbNodeInfo& info);                ///< slot 25
        virtual void        startEcho(hkReal echoDuration);                                          ///< slot 27

        /// @brief The index in m_states of the state `stateId`, -1 for none.
        /// @note Id 0 is not in m_stateIdToIndexMap: it is the first state's id or no state's.
        int getStateIndex(int stateId) const;

        /// @brief Adds a state (id 0 always goes first).
        /// @note Name provisional.
        void addState(int stateId, hkbGenerator* generator, const char* name);
        /// @brief Removes the state `stateId` and every transition to it (the last state takes its index).
        /// @note Name provisional.
        void removeState(int stateId);
        /// @brief The lowest state id no state has.
        /// @note Name provisional.
        int getFirstFreeStateId() const;
        /// @brief Gives state `oldStateId` the id `newStateId` (swapping with the state that had it), retargeting the
        ///        transitions.
        /// @note Name provisional.
        void swapStateIds(int oldStateId, int newStateId);
        /// @brief The generator that plays: the newest transition effect, else the current state's generator.
        /// @note Name provisional.
        hkbGenerator* getActiveGenerator() const;
        /// @brief Whether a delayed transition's from-generator has reached its end; Havok's abut-at-end test.
        /// @note Name provisional. Always false in this build (Bethesda dropped FLAG_ABUT_AT_END_OF_FROM_GENERATOR, see
        ///       isValid), so update begins a delayed transition only once its initiate interval opens.
        bool isFromGeneratorAtEnd(hkbBehaviorGraph* rootBehavior, hkReal timestep) const;
        /// @brief Restarts the start state after an echo (startEcho), as m_selfTransitionMode allows; then begins the
        ///        first delayed transition that is in its initiate interval (any, under `force`) and drops the others.
        /// @note Name provisional.
        void processDelayedTransitions(const hkbContext& context, bool force);
        /// @brief Goes to state `toStateId`: through the transitions that lead there when one can interrupt, else (when
        ///        `forceIfNoTransition`, or when inactive or `immediate`) by ending every transition and switching.
        /// @note Name provisional.
        void transitionToState(int toStateId, const hkbContext& context, bool immediate, bool forceIfNoTransition, bool isReturnToPreviousState);
        /// @brief Picks the transition effect for `transition`: the highest-priority transition to its to-state (from the
        ///        current state or a local wildcard) when that outranks the transition itself; then begins it.
        /// @note Name provisional.
        void beginTransitionWithBestEffect(const hkbContext& context, ProspectiveTransitionInfo& transition);
        /// @brief Ends each active transition the generators no longer play (inactive, done, or no longer a child of the
        ///        newer transition), together with every older one.
        /// @note Name provisional (vanilla's monitor timer around the call: "StUpdateActiveTransitions").
        void updateActiveTransitions(const hkbContext& context);
        /// @brief The transition `ref` names.
        HK_FORCE_INLINE TransitionInfo* getTransitionInfo(hkbBehaviorGraph* rootBehavior, const TransitionInfoReference& ref);
        /// @brief Removes every transition to `toStateId`, from every state and from the wildcards.
        /// @note Name provisional.
        void removeTransitionsToState(int toStateId);
        /// @brief Removes (swapping in the last) every transition of `transitions` (may be null) to `toStateId`, releasing
        ///        its effect and condition.
        /// @note Name provisional.
        static void removeTransitions(int toStateId, TransitionInfoArray* transitions);
        /// @brief Adds the event ids of `transitions` (may be null) to `activeEvents`: each transition's own event, and its
        ///        trigger and initiate intervals' events when those intervals are used.
        /// @note Name provisional.
        static void getActiveEventsOfTransitions(const TransitionInfoArray* transitions, hkPointerMap<int, bool>& activeEvents);


        /// @name Defined elsewhere in vanilla's unit (pinned, not reimplemented yet).
        /// @{
        /// @brief Ends the active transitions [0, index] (and any after them heading to the same state).
        /// @note Name provisional.
        void endTransitions(int index, const hkbContext& context);
        /// @note Name provisional.
        void exitCurrentState(const hkbContext& context);
        /// @note Name provisional.
        void enterCurrentState(const hkbContext& context);
        /// @note Name provisional.
        void handleEventInternal(const hkbContext& context, int eventId);
        /// @brief The state to start in (per m_startStateMode), with a warning when that state is missing or disabled.
        /// @note Name provisional.
        int  chooseStartStateId(const hkbContext& context);
        /// @note Name provisional.
        void setCurrentState(int stateId, bool isReturnToPreviousState, const hkbContext& context, bool flag);
        /// @brief Writes `stateId` to the sync variable, when there is one.
        /// @note Name provisional.
        void writeSyncVariable(const hkbContext& context, int stateId);
        /// @note Name provisional.
        void beginTransition(const hkbContext& context, const ProspectiveTransitionInfo& transition, bool isReturnToPreviousState);
        /// @brief Whether the newest active transition and every delayed one may be interrupted.
        /// @note Name provisional.
        bool isInterruptible(hkbBehaviorGraph* rootBehavior);
        /// @brief Collects into `transitionsOut` the transitions of `transitions` that `eventId` (or any, under `anyEvent`)
        ///        would take to `toStateId` (or anywhere, under `anyToState`), keeping only the highest priority.
        /// @note Name provisional. `fromStateIndex` -1 means the wildcards; `localWildcardsOnly` keeps only local
        ///       wildcards; `nestedStateMachine` (may be null) checks FLAG_FROM_NESTED_STATE_ID_IS_VALID.
        void getTransitions(hkbStateMachine* nestedStateMachine, int fromStateIndex, int eventId, int toStateId, bool anyEvent, bool anyToState,
                            bool localWildcardsOnly, const hkbContext& context, hkArray<TransitionInfoReference>& transitionsOut);
        /// @note Name provisional.
        void findTransitions(hkbBehaviorGraph* rootBehavior, int eventId, int fromStateId, int toStateId, bool anyEvent, bool flag,
                             hkbSymbolIdMap* eventIdMap, const hkbContext& context, hkArray<ProspectiveTransitionInfo>& transitionsOut);
        /// @note Name provisional.
        void beginTransitions(const hkbContext& context, hkArray<ProspectiveTransitionInfo>& transitions, int eventId, bool flag,
                              bool isReturnToPreviousState);
        /// @note Name provisional.
        void initRuntimeData();
        /// @note Name provisional.
        void setCurrentStateId(int stateId);
        /// @}
    };
    static_assert(sizeof(hkbStateMachine) == 0x108, "hkbStateMachine size");
    static_assert(sizeof(hkbStateMachine::TransitionInfo) == 0x48, "TransitionInfo size");
    static_assert(sizeof(hkbStateMachine::ActiveTransitionInfo) == 0x28, "ActiveTransitionInfo size");
    static_assert(sizeof(hkbStateMachine::DelayedTransitionInfo) == 0x18, "DelayedTransitionInfo size");
    static_assert(sizeof(hkbStateMachine::StateInfo) == 0x78, "StateInfo size");

    /// @brief What getInternalState saves and setInternalState restores.
    struct hkbStateMachineInternalState : hkReferencedObject
    {
        hkArray<hkbStateMachine::ActiveTransitionInfo>  m_activeTransitions;             ///< +0x10
        hkArray<hkUint8>                                m_transitionFlags;               ///< +0x20
        hkArray<hkUint8>                                m_wildcardTransitionFlags;       ///< +0x30
        hkArray<hkbStateMachine::DelayedTransitionInfo> m_delayedTransitions;            ///< +0x40
        hkReal                                          m_timeInState;                   ///< +0x50
        hkReal                                          m_lastLocalTime;                 ///< +0x54
        hkInt32                                         m_currentStateId;                ///< +0x58
        hkInt32                                         m_previousStateId;               ///< +0x5c
        hkInt32                                         m_nextStartStateIndexOverride;   ///< +0x60
        hkBool                                          m_stateOrTransitionChanged;      ///< +0x64
        hkBool                                          m_echoNextUpdate;                ///< +0x65
    };
} }
