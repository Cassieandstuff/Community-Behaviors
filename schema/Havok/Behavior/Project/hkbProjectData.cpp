/// @file
/// @brief hkbProjectData, reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/Project/hkbProjectData.h>

namespace CB { namespace schema
{
    hkbProjectData::hkbProjectData(hkFinishLoadedObjectFlag flag) : hkReferencedObject(flag), m_stringData(flag)
    {
    }
} }
