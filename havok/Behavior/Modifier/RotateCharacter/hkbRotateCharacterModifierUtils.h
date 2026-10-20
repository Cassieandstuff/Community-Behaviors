/// @file
/// @brief hkbRotateCharacterModifierUtils: the world-transform work behind hkbRotateCharacterModifier::modify.
/// @note A file of its own in vanilla: the function sits at 0x140b0ee80, far from the class's own functions, in a run of
///       out-of-line modify helpers of a dozen node classes (damping, foot IK controls, mirror, keyframe bones, twist,
///       blending transition…), each called only from its class — Havok's `…Utils` split, as the exe's
///       Modifier\BSLookAt\BSLookAtModifierUtils.cpp is for BSLookAtModifier. The file name is inferred.
#pragma once

#include <Behavior/Modifier/RotateCharacter/hkbRotateCharacterModifier.h>

namespace CB { namespace havok
{
    namespace hkbRotateCharacterModifierUtils
    {
        /// @brief Applies the modifier's pending rotation (m_angle about m_axisOfRotation, or about `modelUp` when the
        ///        axis is zero) to `worldFromModel` in the frame of `characterWorldFromModel`, then clears m_angle.
        /// @note Name provisional.
        void modify(hkbRotateCharacterModifier& mod, const hkQsTransform& characterWorldFromModel, const hkVector4& modelUp,
                    hkQsTransform& worldFromModel);
    }
} }
