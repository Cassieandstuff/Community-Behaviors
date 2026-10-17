/// @file
/// @brief hkbHandIkDriverInfo, reimplemented against vanilla (AE 1.6.1170).
/// @note The destructor stays implicit: vanilla's out-of-line copy (0x140ac6a50) stores no vtable of its own.
#include <Behavior/Driver/HandIk/hkbHandIkDriverInfo.h>

namespace CB { namespace schema
{
    hkbHandIkDriverInfo::hkbHandIkDriverInfo(hkFinishLoadedObjectFlag flag)
    :   hkReferencedObject(flag),
        m_hands(flag)
    {
    }
} }
