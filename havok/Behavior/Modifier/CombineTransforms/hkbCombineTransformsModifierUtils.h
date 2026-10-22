/// @file
/// @brief hkbCombineTransformsModifierUtils: the transform product behind hkbCombineTransformsModifier::modify.
/// @note A file of its own in vanilla: the function sits at 0x140b0cc90, far from the class's own functions, in the run
///       of out-of-line helpers of a dozen node classes (0x140b0c450–0x140b0fb0f) — Havok's `…Utils` split, as the exe's
///       Modifier\BSLookAt\BSLookAtModifierUtils.cpp is for BSLookAtModifier. The file name is inferred.
#pragma once

#include <Behavior/Modifier/CombineTransforms/hkbCombineTransformsModifier.h>

namespace CB { namespace havok
{
    namespace hkbCombineTransformsModifierUtils
    {
        /// @brief (m_translationOut, m_rotationOut) = left · right, each side inverted when its flag asks, and the
        ///        product inverted under m_invertResult (as right⁻¹ · left⁻¹, flipping each side's inversion).
        /// @note Name provisional. "Inverting" a side negates its translation and conjugates its rotation — vanilla's
        ///       shortcut, not the transform's true inverse unless one of the two is trivial.
        void modify(hkbCombineTransformsModifier& modifier);

        /// @brief (translationOut, rotationOut) = (translationA + rotationA·translationB, rotationA·rotationB).
        /// @note Name provisional; 0x140b0cd70, called only from modify.
        void multiply(const hkVector4& translationA, const hkQuaternion& rotationA, const hkVector4& translationB,
                      const hkQuaternion& rotationB, hkVector4& translationOut, hkQuaternion& rotationOut);
    }
} }
