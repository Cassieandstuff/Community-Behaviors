/// @file
/// @brief hkbBoneIndexArray, reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/Utils/hkbBoneIndexArray.h>

namespace CB { namespace schema
{
    hkbBoneIndexArray::hkbBoneIndexArray(hkFinishLoadedObjectFlag flag) : hkbBindable(flag), m_boneIndices(flag)
    {
    }
} }
