/// @file
/// @brief hkbBehaviorGraphInternalState, reimplemented against vanilla (AE 1.6.1170).
/// @note The class is declared in hkbBehaviorGraph.h; its destructor stays implicit (vanilla's out-of-line copy,
///       0x140ac5ef0, has no vtable store of its own).
#include <Behavior/BehaviorGraph/hkbBehaviorGraph.h>

namespace CB { namespace schema
{
    hkbBehaviorGraphInternalState::hkbBehaviorGraphInternalState(hkFinishLoadedObjectFlag flag)
    :   hkReferencedObject(flag), m_nodeInternalStateInfos(flag), m_variableValueSet(flag)
    {
    }
} }
