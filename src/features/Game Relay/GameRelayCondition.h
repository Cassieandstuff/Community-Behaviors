#pragma once

#include <Common/Base/hkBase.h>
#include <Behavior/Behavior/Condition/hkbCondition.h>
#include <Behavior/Behavior/Context/hkbContext.h>

#include <atomic>
#include <cstddef>

namespace hky::relay { class CompiledExpr; }

/// @file
/// @brief CBGameRelayCondition, CB's own Havok condition: true when its Animation Relay expression holds for the
///        character's actor. The layout is schema/events/CBGameRelayCondition.yaml's: the platform registers the class
///        from that descriptor (platform/classes) and checks the sizes agree.
/// @note  Why a condition class: hkbCondition::isTrue is Havok's own seam for "should this happen", read by state
///        machine transitions (and by CB's selector rows), so game state reaches a graph without a hook or a variable.

namespace CB::feature::relay {

    class GameRelayCondition : public ::hkbCondition {
    public:
        ::hkStringPtr m_expression;   ///< +0x10, Relay text, byte-exact
        /// @brief +0x18, runtime: the expression compiled, null until the first isTrue.
        /// @invariant Once set it never changes: it points into a process-wide cache keyed by the exact text.
        std::atomic<const hky::relay::CompiledExpr*> m_compiled;

        /// @brief The finish constructor: the vtable on loaded bytes; `m_expression` stays as loaded.
        explicit GameRelayCondition(::hkFinishLoadedObjectFlag flag);

        const ::hkClass* getClassType() const override;
        /// @return the expression's value as a bool; false when it does not compile or the character has no actor, as
        ///         hkbExpressionCondition answers false when its expression fails to evaluate.
        /// @note  One instance serves every character's clone of its state machine (a condition is not a node, so it
        ///        is never cloned), possibly on several of Havok's graph threads at once: per-call state lives on the
        ///        call, never on the object.
        bool isTrue(const ::hkbContext& context) override;
    };
    static_assert(sizeof(GameRelayCondition) == 0x20, "GameRelayCondition size (schema: 32)");
    static_assert(offsetof(GameRelayCondition, m_expression) == 0x10);
    static_assert(offsetof(GameRelayCondition, m_compiled) == 0x18);

}  // namespace CB::feature::relay
