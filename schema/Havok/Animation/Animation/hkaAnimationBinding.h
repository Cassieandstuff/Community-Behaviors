/// @file
/// @brief hkaAnimationBinding: an animation bound to a skeleton's tracks.
#pragma once

#include <Animation/Animation/hkaAnimation.h>

namespace CB { namespace schema
{
    struct hkaAnimationBinding : hkReferencedObject
    {
        enum BlendHint { NORMAL = 0, ADDITIVE = 1 };

        hkStringPtr            m_originalSkeletonName;   ///< +0x10
        hkRefPtr<hkaAnimation> m_animation;                      ///< +0x18
        hkArray<hkInt16>       m_transformTrackToBoneIndices;    ///< +0x20, empty = track i drives bone i
        hkArray<hkInt16>       m_floatTrackToFloatSlotIndices;   ///< +0x30, empty = track i drives slot i
        hkInt8                 m_blendHint;                      ///< +0x40

        HK_FORCE_INLINE hkaAnimationBinding() {}
        HK_FORCE_INLINE hkaAnimationBinding(hkFinishLoadedObjectFlag flag)
        :   hkReferencedObject(flag),
            m_originalSkeletonName(flag),
            m_animation(flag),
            m_transformTrackToBoneIndices(flag),
            m_floatTrackToFloatSlotIndices(flag)
        {
        }
    };
} }
