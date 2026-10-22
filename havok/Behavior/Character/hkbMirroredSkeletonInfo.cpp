/// @file
/// @brief hkbMirroredSkeletonInfo, reimplemented against vanilla (AE 1.6.1170).
/// @note The class is declared in hkbCharacter.h.
#include <Behavior/Character/hkbCharacter.h>

namespace CB { namespace havok
{
    hkbMirroredSkeletonInfo::hkbMirroredSkeletonInfo(hkFinishLoadedObjectFlag flag) : hkReferencedObject(flag), m_bonePairMap(flag)
    {
    }
} }
