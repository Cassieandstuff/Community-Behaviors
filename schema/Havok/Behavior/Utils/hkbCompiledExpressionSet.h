/// @file
/// @brief hkbCompiledExpressionSet: behavior expressions compiled to reverse-Polish token lists, and their evaluator.
#pragma once

#include <Common/Base/hkBase.h>
#include <Common/Base/Container/StringMap/hkStringMap.h>

namespace CB { namespace schema
{
    struct hkbContext;

    /// @note 0x38 bytes. The compiler and the evaluator are vanilla's own (called through pins, not reimplemented); the
    ///       destructor is implicit (hkbEvaluateExpressionModifier::isValid inlines it for a stack instance).
    class hkbCompiledExpressionSet : public hkReferencedObject
    {
    public:
        /// @note 8 bytes. Member names after the Havok SDK; only TOKEN_TYPE_VARIABLE_INDEX (3, m_data = the variable
        ///       index as a real) is confirmed, by getActiveVariables.
        struct Token
        {
            enum TokenType { TOKEN_TYPE_VARIABLE_INDEX = 3 };

            hkReal m_data;       ///< +0x0
            hkInt8 m_type;       ///< +0x4 — TokenType
            hkInt8 m_operator;   ///< +0x5
        };

        hkArray<Token> m_rpn;                    ///< +0x10 — every expression's tokens, back to back
        hkArray<int>   m_expressionToRpnIndex;   ///< +0x20 — where each expression starts in m_rpn
        hkInt8         m_numExpressions;         ///< +0x30

        hkbCompiledExpressionSet();

        /// @brief Compiles `expression` (the right-hand side) as the next expression of the set, resolving names
        ///        through the maps (null maps: syntax check only). Errors are appended to `errorString`.
        /// @note Name provisional.
        bool compile(const char* expression, hkStringMap<int>* variableNameToIndex, hkStringMap<int>* characterPropertyNameToIndex,
                     hkStringBuf& errorString);
        /// @brief Evaluates expression `expressionIndex` against the context's variables into `result`; false on an
        ///        evaluation error.
        /// @note Name provisional; shared with hkbExpressionCondition.
        bool evaluate(const hkbContext& context, int expressionIndex, hkReal& result) const;
        /// @brief Adds every variable the compiled expressions read into `activeVariables` (index → true).
        /// @note Name provisional.
        void getActiveVariables(hkPointerMap<int, bool>& activeVariables) const;
    };
} }
