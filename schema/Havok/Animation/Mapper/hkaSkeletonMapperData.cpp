/// @file
/// @brief hkaSkeletonMapperData, reimplemented against vanilla (AE 1.6.1170).
#include <Animation/Mapper/hkaSkeletonMapper.h>

namespace CB { namespace schema
{
    hkaSkeletonMapperData::hkaSkeletonMapperData(hkFinishLoadedObjectFlag flag)
    :   m_skeletonA(flag), m_skeletonB(flag), m_simpleMappings(flag), m_chainMappings(flag), m_unmappedBones(flag)
    {
    }

    hkaSkeletonMapperData::~hkaSkeletonMapperData()
    {
    }
} }
