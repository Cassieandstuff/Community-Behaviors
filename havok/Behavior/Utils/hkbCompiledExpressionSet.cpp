/// @file
/// @brief hkbCompiledExpressionSet, reimplemented against vanilla (AE 1.6.1170) — the constructor and getActiveVariables so far (the compiler and evaluator stay vanilla's).
#include <Behavior/Utils/hkbCompiledExpressionSet.h>

namespace CB { namespace havok
{
    hkbCompiledExpressionSet::hkbCompiledExpressionSet() : m_numExpressions(0)
    {
    }

    void hkbCompiledExpressionSet::getActiveVariables(hkPointerMap<int, bool>& activeVariables) const
    {
        const int numTokens = m_rpn.getSize();
        for (int i = 0; i < numTokens; i++) {
            if (m_rpn[i].m_type == Token::TOKEN_TYPE_VARIABLE_INDEX) activeVariables.insert(static_cast<int>(m_rpn[i].m_data), true);
        }
    }
} }
