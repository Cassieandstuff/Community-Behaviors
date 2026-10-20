/// @file
/// @brief hkbExpressionCondition: a condition given as a behavior expression, true when it evaluates nonzero.
#pragma once

#include <Behavior/Condition/hkbCondition.h>
#include <Behavior/Utils/hkbCompiledExpressionSet.h>

namespace CB { namespace havok
{
    /// @note 0x20 bytes.
    class hkbExpressionCondition : public hkbCondition
    {
    public:
        hkStringPtr                        m_expression;              ///< +0x10
        /// @note Not serialized: the expression compiled (as expression 0) when the graph is set up.
        hkRefPtr<hkbCompiledExpressionSet> m_compiledExpressionSet;   ///< +0x18

        HK_FORCE_INLINE hkbExpressionCondition() {}
        /// @note Vanilla inlines it into the type registry's finish function; out of line here, it also brings the
        ///       class's vtable and scalar deleting destructor into this unit.
        hkbExpressionCondition(hkFinishLoadedObjectFlag flag);

        /// @note False when the expression fails to evaluate.
        virtual bool isTrue(const hkbContext& context);   ///< slot 3
    };
} }
