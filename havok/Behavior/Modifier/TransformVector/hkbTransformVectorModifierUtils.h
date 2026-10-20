/// @file
/// @brief hkbTransformVectorModifierUtils: the vector transform behind hkbTransformVectorModifier's activate and modify.
/// @note A file of its own in vanilla: the function sits at 0x140b0f2e0, far from the class's own functions, in the run
///       of out-of-line helpers of a dozen node classes (0x140b0c450–0x140b0fb0f) — Havok's `…Utils` split, as the exe's
///       Modifier\BSLookAt\BSLookAtModifierUtils.cpp is for BSLookAtModifier. The file name is inferred.
#pragma once

#include <Behavior/Modifier/TransformVector/hkbTransformVectorModifier.h>

namespace CB { namespace havok
{
    namespace hkbTransformVectorModifierUtils
    {
        /// @brief m_vectorOut = m_vectorIn transformed by (m_translation, m_rotation), or by its inverse; rotation only
        ///        under m_rotateOnly.
        /// @note Name provisional.
        void computeVectorOut(hkbTransformVectorModifier& modifier);
    }
} }
