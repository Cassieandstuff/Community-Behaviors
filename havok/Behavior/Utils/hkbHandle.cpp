/// @file
/// @brief hkbHandle, reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/Utils/hkbHandle.h>

namespace CB { namespace havok
{
    hkbHandle::hkbHandle(hkFinishLoadedObjectFlag flag) : hkReferencedObject(flag)
    {
    }
} }
