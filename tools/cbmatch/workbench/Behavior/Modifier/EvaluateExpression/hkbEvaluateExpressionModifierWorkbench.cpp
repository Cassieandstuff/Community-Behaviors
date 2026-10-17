/// @file
/// @brief Workbench: the functions of hkbEvaluateExpressionModifier.cpp still being matched (tools/cbmatch/parked.yaml says why each is
///        stuck). Compiled only by tools/cbmatch/manifests/workbench.yaml; a body that matches graduates back into
///        schema/Havok/Behavior/Modifier/EvaluateExpression/hkbEvaluateExpressionModifier.cpp.
#include <Behavior/Modifier/EvaluateExpression/hkbEvaluateExpressionModifier.h>
#include <Behavior/BehaviorGraph/hkbBehaviorGraph.h>
#include <Behavior/Event/hkbEventQueue.h>

namespace CB { namespace schema
{
    namespace
    {
        /// @brief A word value read as T (a real through an integer copy, as vanilla converts it).
        template <typename T> HK_FORCE_INLINE T getWord(const hkbVariableValue& v) { return *reinterpret_cast<const T*>(&v.m_value); }
        template <> HK_FORCE_INLINE hkReal getWord<hkReal>(const hkbVariableValue& v)
        {
            union { hkInt32 i; hkReal r; } u;
            u.i = v.m_value;
            return u.r;
        }
        template <typename T> HK_FORCE_INLINE void setWord(hkbVariableValue& v, T value) { v.m_value = value; }
        template <> HK_FORCE_INLINE void setWord<hkReal>(hkbVariableValue& v, hkReal value)
        {
            union { hkInt32 i; hkReal r; } u;
            u.r       = value;
            v.m_value = u.i;
        }

        /// @brief `value` clamped to [minValue, maxValue].
        /// @note Two spellings for one behaviour: integers select the upper bound with a conditional move, reals branch
        ///       to it (the integer spelling compiles to minss on a real).
        template <typename T> HK_FORCE_INLINE T clampWord(T value, T minValue, T maxValue)
        {
            if (value < minValue) return minValue;
            return (value > maxValue) ? maxValue : value;
        }
        template <> HK_FORCE_INLINE hkReal clampWord<hkReal>(hkReal value, hkReal minValue, hkReal maxValue)
        {
            if (value < minValue) return minValue;
            return (value <= maxValue) ? value : maxValue;
        }

        /// @brief Writes `value` to word variable `index` of `root`, clamped to its bounds when `clamp`.
        template <typename T> HK_FORCE_INLINE void setVariableValueWord(hkbBehaviorGraph* root, int index, T value, bool clamp)
        {
            if (clamp) {
                const T maxValue = getWord<T>(root->m_data->m_wordMaxVariableValues[index]);
                const T minValue = getWord<T>(root->m_data->m_wordMinVariableValues[index]);
                value = clampWord<T>(value, minValue, maxValue);
            }
            setWord<T>(root->m_variableValueSet->m_wordVariableValues[index], value);
        }
    }

    /// @note The type comes from the context's behavior, the bounds and the value set from the root graph (at the
    ///       variable's root index); a root without bounds data does not clamp.
    void hkbEvaluateExpressionModifier::setVariable(int variableIndex, const hkbContext& context, hkReal value)
    {
        const hkbBehaviorGraphData* data      = context.m_behavior->m_data;
        const int                   rootIndex = hkbSymbolIdMap::convertInternalId(context.m_behavior->m_variableIdMap, context.m_character->m_behaviorGraph->m_variableIdMap, variableIndex);
        hkbBehaviorGraph*           root      = context.m_character->m_behaviorGraph;
        bool                        clamp     = false;
        if (root->m_data->m_wordMinVariableValues.getSize() > 0) clamp = true;

        switch (data->m_variableInfos[variableIndex].m_type) {
            case hkbVariableInfo::VARIABLE_TYPE_BOOL:  setVariableValueWord<bool>(root, rootIndex, value != 0.0f, false); break;
            case hkbVariableInfo::VARIABLE_TYPE_INT8:  setVariableValueWord<hkInt8>(root, rootIndex, static_cast<hkInt8>(static_cast<int>(value)), clamp); break;
            case hkbVariableInfo::VARIABLE_TYPE_INT16: setVariableValueWord<hkInt16>(root, rootIndex, static_cast<hkInt16>(static_cast<int>(value)), clamp); break;
            case hkbVariableInfo::VARIABLE_TYPE_INT32: setVariableValueWord<hkInt32>(root, rootIndex, static_cast<hkInt32>(value), clamp); break;
            case hkbVariableInfo::VARIABLE_TYPE_REAL:  setVariableValueWord<hkReal>(root, rootIndex, value, clamp); break;
        }
    }

} }
