/// @file
/// @brief hkbSymbolIdMap, reimplemented against vanilla (AE 1.6.1170).
/// @note The destructor stays implicit: vanilla's out-of-line copy (0x140b13550) has no vtable store of its own.
#include <Behavior/Utils/hkbSymbolIdMap.h>

namespace CB { namespace schema
{
    hkbSymbolIdMap::hkbSymbolIdMap(hkFinishLoadedObjectFlag flag) : hkReferencedObject(flag), m_internalToExternalMap(flag)
    {
    }
} }
