/// @file
/// @brief hkbBehaviorGraphStringData, reimplemented against vanilla (AE 1.6.1170).
/// @note The class is declared in hkbBehaviorGraph.h; its destructor stays implicit (vanilla's out-of-line copy,
///       0x140ac5fa0, has no vtable store of its own).
#include <Behavior/BehaviorGraph/hkbBehaviorGraph.h>

namespace CB { namespace schema
{
    hkbBehaviorGraphStringData::hkbBehaviorGraphStringData(hkFinishLoadedObjectFlag flag)
    :   hkReferencedObject(flag), m_eventNames(flag), m_attributeNames(flag), m_variableNames(flag), m_characterPropertyNames(flag)
    {
    }
} }
