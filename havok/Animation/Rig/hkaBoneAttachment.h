/// @file
/// @brief hkaBoneAttachment: an object attached to a skeleton bone.
#pragma once

#include <Common/Base/hkBase.h>

namespace CB { namespace havok
{
    struct hkaBoneAttachment : hkReferencedObject
    {
        hkStringPtr                  m_originalSkeletonName;   ///< +0x10
        hkMatrix4                    m_boneFromAttachment;     ///< +0x20
        hkRefPtr<hkReferencedObject> m_attachment;             ///< +0x60
        hkStringPtr                  m_name;                   ///< +0x68
        hkInt16                      m_boneIndex;              ///< +0x70

        HK_FORCE_INLINE hkaBoneAttachment(hkFinishLoadedObjectFlag flag)
        :   hkReferencedObject(flag), m_originalSkeletonName(flag), m_attachment(flag), m_name(flag) {}
    };
} }
