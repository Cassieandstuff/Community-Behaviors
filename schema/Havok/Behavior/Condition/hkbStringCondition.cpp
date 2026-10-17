/// @file
/// @brief hkbStringCondition, reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/Condition/hkbStringCondition.h>

namespace CB { namespace schema
{
    hkbStringCondition::hkbStringCondition(hkFinishLoadedObjectFlag flag) : hkbCondition(flag), m_conditionString(flag)
    {
    }

    bool hkbStringCondition::isTrue(const hkbContext& context)
    {
        if (s_evaluateFunction == HK_NULL) {
            return s_defaultResult;
        }
        return s_evaluateFunction(m_conditionString.cString(), context);
    }
} }
