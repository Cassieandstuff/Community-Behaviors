/// @file
/// @brief hkbBehaviorGraphData, reimplemented against vanilla (AE 1.6.1170).
/// @note The class is declared in hkbBehaviorGraph.h.
#include <Behavior/BehaviorGraph/hkbBehaviorGraph.h>

namespace CB { namespace schema
{
    hkbBehaviorGraphData::~hkbBehaviorGraphData()
    {
        m_variableInitialValues->removeReference();
    }
} }
