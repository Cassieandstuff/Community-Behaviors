/// @file
/// @brief hkbEvaluateExpressionModifier: evaluates `variable = expression` / `event if expression` lines each update,
///        writing the variable or raising the event.
#pragma once

#include <Behavior/Context/hkbContext.h>
#include <Behavior/Modifier/hkbModifier.h>
#include <Behavior/Utils/hkbCompiledExpressionSet.h>

namespace CB { namespace schema
{
    /// @note 0x18 bytes. Member names are the reflected ones.
    struct hkbExpressionData
    {
        enum ExpressionEventMode
        {
            EVENT_MODE_SEND_ONCE                  = 0,
            EVENT_MODE_SEND_ON_TRUE               = 1,
            EVENT_MODE_SEND_ON_FALSE_TO_TRUE      = 2,
            EVENT_MODE_SEND_EVERY_FRAME_ONCE_TRUE = 3,
        };

        hkStringPtr m_expression;                ///< +0x00
        hkInt32     m_assignmentVariableIndex;   ///< +0x08 — -1 = none
        hkInt32     m_assignmentEventIndex;      ///< +0x0c — -1 = none
        hkInt8      m_eventMode;                 ///< +0x10 — ExpressionEventMode
        hkBool      m_raisedEvent;               ///< +0x11
        hkBool      m_wasTrueInPreviousFrame;    ///< +0x12

        /// @note Written out: the implicit ones stay out of line under /Ob1.
        HK_FORCE_INLINE hkbExpressionData(const hkbExpressionData& d)
        :   m_expression(d.m_expression),
            m_assignmentVariableIndex(d.m_assignmentVariableIndex),
            m_assignmentEventIndex(d.m_assignmentEventIndex),
            m_eventMode(d.m_eventMode),
            m_raisedEvent(d.m_raisedEvent),
            m_wasTrueInPreviousFrame(d.m_wasTrueInPreviousFrame) {}
        HK_FORCE_INLINE hkbExpressionData& operator=(const hkbExpressionData& d)
        {
            m_expression              = d.m_expression;
            m_assignmentVariableIndex = d.m_assignmentVariableIndex;
            m_assignmentEventIndex    = d.m_assignmentEventIndex;
            m_eventMode               = d.m_eventMode;
            m_raisedEvent             = d.m_raisedEvent;
            m_wasTrueInPreviousFrame  = d.m_wasTrueInPreviousFrame;
            return *this;
        }
    };

    class hkbExpressionDataArray : public hkReferencedObject
    {
    public:
        hkArray<hkbExpressionData> m_expressionsData;   ///< +0x10

        /// @brief A deep copy (each modifier clone evaluates its own copy).
        hkbExpressionDataArray(const hkbExpressionDataArray& other);
    };

    /// @brief An expression's event state, as the internal state saves it.
    struct hkbEvaluateExpressionModifierInternalExpressionData
    {
        hkBool m_raisedEvent;              ///< +0x0
        hkBool m_wasTrueInPreviousFrame;   ///< +0x1

        /// @note User-provided (and empty): hkArray::setSize then constructs nothing, as in vanilla (a value-initialized
        ///       aggregate would be zeroed).
        HK_FORCE_INLINE hkbEvaluateExpressionModifierInternalExpressionData() {}
    };

    class hkbEvaluateExpressionModifierInternalState : public hkReferencedObject
    {
    public:
        hkArray<hkbEvaluateExpressionModifierInternalExpressionData> m_internalExpressionsData;   ///< +0x10
    };

    /// @note Sized 0x70. Member names are the reflected ones; the `slot N` annotations are vanilla's vtable slots. The
    ///       destructor is implicit: vanilla's (0x140ac6730) is a COMDAT copy the deleting destructor calls. Vanilla's
    ///       translation unit has no default constructor.
    class hkbEvaluateExpressionModifier : public hkbModifier
    {
    public:
        hkRefPtr<hkbExpressionDataArray>                             m_expressions;               ///< +0x50
        hkRefPtr<hkbCompiledExpressionSet>                           m_compiledExpressionSet;     ///< +0x58 — built by precompute
        hkArray<hkbEvaluateExpressionModifierInternalExpressionData> m_internalExpressionsData;   ///< +0x60

        /// @brief The cloneNode copy: its own copy of the expressions, sharing the compiled set.
        hkbEvaluateExpressionModifier(const hkbEvaluateExpressionModifier& mod);

        static const hkClass& staticClass();

        virtual const void*         getClassType() const;                                         ///< slot 1
        virtual void                activate(const hkbContext& context);                          ///< slot 4
        virtual void                update(const hkbContext& context, hkReal timestep);           ///< slot 5
        virtual bool                isValid(hkStringPtr& errorString) const;                      ///< slot 10
        virtual void                precompute(const hkbContext& context);                        ///< slot 11
        virtual hkbNode*            cloneNode(hkbBehaviorGraph& rootBehavior) const;              ///< slot 12
        virtual hkReferencedObject* createInternalState();                                        ///< slot 13
        virtual void                getInternalState(hkReferencedObject& internalState) const;   ///< slot 14
        virtual void                getInternalStateUser(const hkbBehaviorGraph& rootBehavior, hkReferencedObject& internalState);   ///< slot 15
        virtual void                setInternalState(const hkReferencedObject& internalState);   ///< slot 16
        virtual void                setInternalStateUser(const hkbContext& context, const hkReferencedObject& internalState,
                                                         hkPointerMap<hkInt16, const hkbNodeInternalStateInfo*>& nodeIdToInternalStateMap);   ///< slot 17
        virtual void                getActiveVariables(hkPointerMap<int, bool>& activeVariables) const;   ///< slot 20

        /// @brief Splits the assignment off `expression` (tabs and newlines first become spaces): `name if …` names an
        ///        event, `name = …` a variable; `expression` keeps the right-hand side. Names resolve through the context's
        ///        behavior / `variableNameToIndex` when given (null: syntax check only); errors go to `errorString`.
        /// @note Name provisional.
        static bool parseAssignment(hkStringBuf& expression, hkStringBuf& errorString, hkInt32& eventIndexOut, hkInt32& variableIndexOut,
                                    const hkbContext* context, hkStringMap<int>* variableNameToIndex);
        /// @brief The index of the event named `eventName` in `behavior`'s string data, else -1 (case-sensitive).
        /// @note Name provisional.
        static int findEventIndex(const hkbBehaviorGraph* behavior, const char* eventName);
        /// @brief Writes `value` to variable `variableIndex` of the context's behavior, converted to the variable's type
        ///        and clamped to its bounds, in the root graph's value set.
        /// @note Name provisional.
        static void setVariable(int variableIndex, const hkbContext& context, hkReal value);
        /// @brief Strips leading and trailing spaces.
        /// @note Name provisional.
        static void trimWhitespace(hkStringBuf& s);
    };
} }
