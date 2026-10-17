/// @file
/// @brief hkaMirroredSkeleton, reimplemented against vanilla (AE 1.6.1170).
#include <Animation/Rig/hkaSkeleton.h>

namespace CB { namespace schema
{
    hkaMirroredSkeleton::~hkaMirroredSkeleton()
    {
        hkAlignedDeallocate(m_bonePairMap);
        hkAlignedDeallocate(m_boneMirrorRotations);
    }
} }
