/// @file
/// @brief BSLookAtModifierUtils: BSLookAtModifier's per-frame work, compiled apart (Modifier\BSLookAt\BSLookAtModifierUtils.cpp).
#pragma once

#include <Behavior/Modifier/BSLookAt/BSLookAtModifier.h>

namespace CB { namespace havok
{
    class hkaPose;

    /// @brief The fraction of the way a gain moves a value in one step of `timeStep` seconds: gain × timeStep × 30,
    ///        clamped to [0, 1] (the gains are tuned per 1/30 s frame).
    /// @note Name provisional. 0x140ba2060: the one out-of-line body BSDirectAtModifier, BSInterpValueModifier,
    ///       BSLimbIKModifier and BSLookAtModifier all call (identical copies folded).
    hkReal BSComputeGainForTimeStep(hkReal gain, hkReal timeStep);

    struct BSLookAtModifierUtils
    {
        /// @brief Picks the target (m_targetLocation, or the camera point), decides whether it is outside the limit cone
        ///        (raising m_targetOutOfLimitEvent once on leaving), turns every bone and eye bone toward it, then writes
        ///        `pose` back over the output's pose (all bones set).
        /// @note `context` may be null: the out-of-limits event is then dropped with a warning.
        static void modifyInternal(BSLookAtModifier& modifier, hkaPose& pose, hkbGeneratorOutput& inOut, const hkbContext* context);

        /// @brief Eases one bone's current forward axis toward `targetMS` (by up to `limitAngle` radians from its rest
        ///        axis, only while `lookAt` and the bone is enabled; else back toward rest) and rotates the bone to it;
        ///        returns the angle the bone was asked to turn (0 when it was not).
        /// @note Name provisional (0x140ba2ba0).
        static hkReal modifyBone(const BSLookAtModifier& modifier, BSLookAtModifierBoneData& bone, hkaPose& pose, bool lookAt,
                                 const hkVector4& targetMS, hkReal limitAngle);
    };
} }
