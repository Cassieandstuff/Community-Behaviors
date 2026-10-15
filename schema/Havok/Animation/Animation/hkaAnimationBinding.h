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
        hkRefPtr<hkaAnimation> m_animation;              ///< +0x18
        char                   m_pad20[0x40 - 0x20];
        hkInt8                 m_blendHint;              ///< +0x40
    };
} }
