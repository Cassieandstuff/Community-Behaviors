/// @file
/// @brief hkbBehaviorGraphInternalStateInfo, reimplemented against vanilla (AE 1.6.1170).
/// @note The destructor stays implicit: vanilla's out-of-line copy (0x140f0b4b0) has no vtable store of its own.
#include <Behavior/BehaviorGraph/hkbBehaviorGraphInternalStateInfo.h>

namespace CB { namespace schema
{
    hkbBehaviorGraphInternalStateInfo::hkbBehaviorGraphInternalStateInfo(hkFinishLoadedObjectFlag flag)
    :   hkReferencedObject(flag), m_internalState(flag), m_auxiliaryNodeInfo(flag), m_activeEventIds(flag), m_activeVariableIds(flag)
    {
    }
} }
