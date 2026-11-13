#pragma once

#include <Common/Base/hkBase.h>
#include <Behavior/Behavior/Condition/hkbCondition.h>
#include <Behavior/Behavior/Context/hkbContext.h>

#include <cstddef>

/// @file
/// @brief The scoped conditions: CBScopedCondition (scope, test, arguments) and the tree that combines them
///        (CBConditionAnd / Or / Not). Layouts are schema/events/'s; the types an editor checks are
///        schema/metadata/conditions.json's, and engine-tests holds this evaluator to the same scope and test names.
/// @note  A condition is never cloned: one instance serves every character, possibly on several graph threads at once,
///        so per-call state lives on the call (or the thread's evaluation pass), never on the object.

namespace CB::feature::dynamic {

    /// @brief One evaluation pass on this thread: while it lives, each distinct scope chain resolves once per actor.
    /// @note  Outside a pass (a condition on a state machine transition), every condition resolves its own chains.
    class EvaluationScope {
    public:
        EvaluationScope();
        ~EvaluationScope();
        EvaluationScope(const EvaluationScope&)            = delete;
        EvaluationScope& operator=(const EvaluationScope&) = delete;
    };

    class ConditionAnd : public ::hkbCondition {
    public:
        ::hkArray<::hkbCondition*> m_conditions;   ///< +0x10
        explicit ConditionAnd(::hkFinishLoadedObjectFlag flag);
        const ::hkClass* getClassType() const override;
        bool             isTrue(const ::hkbContext& context) override;
    };
    static_assert(sizeof(ConditionAnd) == 32, "ConditionAnd size (schema: 32)");

    class ConditionOr : public ::hkbCondition {
    public:
        ::hkArray<::hkbCondition*> m_conditions;   ///< +0x10
        explicit ConditionOr(::hkFinishLoadedObjectFlag flag);
        const ::hkClass* getClassType() const override;
        bool             isTrue(const ::hkbContext& context) override;
    };
    static_assert(sizeof(ConditionOr) == 32, "ConditionOr size (schema: 32)");

    class ConditionNot : public ::hkbCondition {
    public:
        ::hkbCondition* m_condition;   ///< +0x10
        explicit ConditionNot(::hkFinishLoadedObjectFlag flag);
        const ::hkClass* getClassType() const override;
        bool             isTrue(const ::hkbContext& context) override;
    };
    static_assert(sizeof(ConditionNot) == 24, "ConditionNot size (schema: 24)");

    /// @brief CBConditionScope.
    struct ConditionScope {
        ::hkStringPtr            m_kind;        ///< +0x00, byte-exact
        ::hkArray<::hkStringPtr> m_arguments;   ///< +0x08
    };
    static_assert(sizeof(ConditionScope) == 24, "ConditionScope size (schema: 24)");

    class ScopedCondition : public ::hkbCondition {
    public:
        ::hkArray<ConditionScope> m_scope;        ///< +0x10, empty: the character's own actor
        ::hkStringPtr             m_test;         ///< +0x20
        ::hkArray<::hkStringPtr>  m_arguments;    ///< +0x28
        ::hkArray<ConditionScope> m_other;        ///< +0x38, IsSame's second chain
        ::hkInt8                  m_quantifier;   ///< +0x48, ConditionQuantifier

        explicit ScopedCondition(::hkFinishLoadedObjectFlag flag);
        const ::hkClass* getClassType() const override;
        /// @return false for an unknown scope or test, a malformed argument, or a scope resolving to nothing.
        bool isTrue(const ::hkbContext& context) override;
    };
    static_assert(sizeof(ScopedCondition) == 80, "ScopedCondition size (schema: 80)");
    static_assert(offsetof(ScopedCondition, m_test) == 0x20);
    static_assert(offsetof(ScopedCondition, m_quantifier) == 0x48);

}  // namespace CB::feature::dynamic
