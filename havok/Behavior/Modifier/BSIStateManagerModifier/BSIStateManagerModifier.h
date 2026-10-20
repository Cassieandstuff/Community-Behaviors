/// @file
/// @brief BSIStateManagerModifier: sets its iState variable from which state of a set of state machines was entered.
#pragma once

#include <Behavior/Context/hkbContext.h>
#include <Behavior/Modifier/hkbModifier.h>
#include <Behavior/StateMachine/hkbStateListener.h>

namespace CB { namespace havok
{
    class hkbStateMachine;

    /// @note Member and nested class names are the class's reflected ones. m_iStateVar is meant to be bound to an
    ///       iState_* graph variable (the exe's member description).
    class BSIStateManagerModifier : public hkbModifier
    {
    public:
        /// @brief Entering state m_StateID of m_pStateMachine sets m_iStateVar to m_iStateToSetAs.
        struct BSiStateData
        {
            hkbStateMachine* m_pStateMachine;   ///< +0x00 — a template node
            hkInt32          m_StateID;         ///< +0x08
            hkInt32          m_iStateToSetAs;   ///< +0x0c

            /// @note User-provided (so not POD): vanilla's array copy constructs the new elements through a
            ///       null-checked placement new. Its body is not observed; it initialises nothing here.
            HK_FORCE_INLINE BSiStateData() {}
        };

        /// @brief The listener the manager's state machines call; it forwards to the manager's clone.
        class BSIStateManagerStateListener : public hkbStateListener
        {
        public:
            /// @note The template manager (the one the graph was built with).
            BSIStateManagerModifier* m_pStateManager;   ///< +0x10

            /// @note Leaves m_pStateManager unset: the manager's default constructor stores it once (vanilla has no null
            ///       store there), where its finish and copy constructors null it first (through the other constructor).
            HK_FORCE_INLINE BSIStateManagerStateListener() {}
            HK_FORCE_INLINE BSIStateManagerStateListener(BSIStateManagerModifier* manager) : m_pStateManager(manager) {}

            virtual void              onEnter(const hkbContext& context, hkbStateMachine* sm);   ///< slot 3
            /// @note Shares itself (one more reference) rather than copying.
            virtual hkbStateListener* clone();                                                   ///< slot 6
        };

        hkInt32                      m_iStateVar;         ///< +0x50
        hkArray<BSiStateData>        m_stateData;         ///< +0x58
        BSIStateManagerStateListener m_myStateListener;   ///< +0x68

        BSIStateManagerModifier();
        /// @note Called by the class's packfile finish function (in the type-registration block).
        BSIStateManagerModifier(hkFinishLoadedObjectFlag flag);
        BSIStateManagerModifier(const BSIStateManagerModifier& mod);
        /// @note User-declared: vanilla's restamps the class's vtable first.
        virtual ~BSIStateManagerModifier();                                               ///< slot 0

        static const hkClass& staticClass();

        virtual const void* getClassType() const;                                         ///< slot 1
        /// @brief At least two entries, each naming a state machine and one of its states.
        /// @note Stops at the first bad entry, its message formatted with the entry's index.
        virtual bool        isValid(hkStringPtr& errorString) const;                      ///< slot 10
        virtual hkbNode*    cloneNode(hkbBehaviorGraph& rootBehavior) const;              ///< slot 12

        /// @brief Sets m_iStateVar from the first entry matching `sm` and the state it is entering.
        /// @note Name provisional. The entered state is the newest transition's target, else the current state.
        void onStateEntered(const hkbContext& context, hkbStateMachine* sm);
    };
    static_assert(sizeof(BSIStateManagerModifier) == 0x80, "BSIStateManagerModifier size");
} }
