/// @file
/// @brief hkbComputeRotationToTargetModifierUtils: the rotation solve behind hkbComputeRotationToTargetModifier::modify.
/// @note A file of its own in vanilla: the function sits at 0x140b0d100, far from the class's own functions, in the run
///       of out-of-line helpers of a dozen node classes (0x140b0c450–0x140b0fb0f) — Havok's `…Utils` split, as the exe's
///       Modifier\BSLookAt\BSLookAtModifierUtils.cpp is for BSLookAtModifier. The file name is inferred.
#pragma once

#include <Behavior/Modifier/ComputeRotationToTarget/hkbComputeRotationToTargetModifier.h>

namespace CB { namespace schema
{
    namespace hkbComputeRotationToTargetModifierUtils
    {
        /// @brief Sets m_rotationOut to the rotation about m_localAxisOfRotation that turns m_localFacingDirection toward
        ///        m_targetPosition as seen from (m_currentPosition, m_currentRotation) — composed onto m_currentRotation
        ///        unless m_resultIsDelta. Both directions are projected onto the plane normal to the axis; m_rotationOut
        ///        is left alone when either projection is degenerate (not finite).
        /// @note Name provisional. Opposite directions (cosine below -0.9999) take the axis itself as the quaternion
        ///       (w = 0: half a turn about it), where the shortest arc is undefined.
        void modify(hkbComputeRotationToTargetModifier& modifier);
    }
} }
