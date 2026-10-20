/// @file
/// @brief hkbFootIkDriverInfo, reimplemented against vanilla (AE 1.6.1170).
/// @note The destructor stays implicit: vanilla's scalar deleting destructor inlines it (no vtable store of its own).
#include <Behavior/Driver/FootIk/hkbFootIkDriverInfo.h>

namespace CB { namespace havok
{
    hkbFootIkDriverInfo::hkbFootIkDriverInfo(hkFinishLoadedObjectFlag flag)
    :   hkReferencedObject(flag),
        m_legs(flag)
    {
    }
} }
