/// @file
/// @brief hkaAnimationContainer: a packfile's top-level animation data (skeletons, animations, bindings, attachments, skins).
#pragma once

#include <Animation/Animation/hkaAnimationBinding.h>
#include <Animation/Deform/Skinning/hkaMeshBinding.h>
#include <Animation/Rig/hkaBoneAttachment.h>
#include <Animation/Rig/hkaSkeleton.h>

namespace CB { namespace schema
{
    struct hkaAnimationContainer : hkReferencedObject
    {
        hkArray<hkRefPtr<hkaSkeleton>>         m_skeletons;     ///< +0x10
        hkArray<hkRefPtr<hkaAnimation>>        m_animations;    ///< +0x20
        hkArray<hkRefPtr<hkaAnimationBinding>> m_bindings;      ///< +0x30
        hkArray<hkRefPtr<hkaBoneAttachment>>   m_attachments;   ///< +0x40
        hkArray<hkRefPtr<hkaMeshBinding>>      m_skins;         ///< +0x50

        HK_FORCE_INLINE hkaAnimationContainer(hkFinishLoadedObjectFlag flag)
        :   hkReferencedObject(flag), m_skeletons(flag), m_animations(flag), m_bindings(flag), m_attachments(flag), m_skins(flag) {}
    };
} }
