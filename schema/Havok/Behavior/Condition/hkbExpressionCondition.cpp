/// @file
/// @brief hkbExpressionCondition, reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/Condition/hkbExpressionCondition.h>

namespace CB { namespace schema
{
    hkbExpressionCondition::hkbExpressionCondition(hkFinishLoadedObjectFlag flag)
    :   hkbCondition(flag), m_expression(flag), m_compiledExpressionSet(flag)
    {
    }

    bool hkbExpressionCondition::isTrue(const hkbContext& context)
    {
        hkReal result = 0.0f;
        if (m_compiledExpressionSet->evaluate(context, 0, result)) {
            return result != 0.0f;
        }
        return false;
    }
} }
