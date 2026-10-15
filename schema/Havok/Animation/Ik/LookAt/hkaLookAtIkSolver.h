/// @file
/// @brief hkaLookAtIkSolver: turns one bone toward a target, within a cone.
#pragma once

#include <Common/Base/hkBase.h>

namespace CB { namespace schema
{
    struct hkaLookAtIkSolver
    {
        struct Setup
        {
            hkVector4 m_fwdLS;           ///< +0x00, the bone's forward axis, bone space
            hkVector4 m_eyePositionLS;   ///< +0x10, the eye's offset, bone space
            hkVector4 m_limitAxisMS;     ///< +0x20, the cone's axis, model space
            hkReal    m_limitAngle;      ///< +0x30, the cone's half-angle, radians
        };

        /// @brief Separate limits per direction about the cone axis, radians.
        /// @note Field order per vanilla's stack layout (up, down, left, right, then the axis); names provisional.
        struct RangeLimits
        {
            hkReal    m_limitAngleUp;      ///< +0x00
            hkReal    m_limitAngleDown;    ///< +0x04
            hkReal    m_limitAngleLeft;    ///< +0x08
            hkReal    m_limitAngleRight;   ///< +0x0c
            hkVector4 m_upAxisMS;          ///< +0x10
        };

        /// @brief Rotates `boneModelSpaceInOut` by up to `gain` toward `targetMS`; whether the target was inside the
        ///        limits (`range`, when not null, else the setup's cone).
        /// @note Out of line in vanilla (0x140be0ee0).
        static hkBool solve(const Setup& setup, const hkVector4& targetMS, hkReal gain, hkQsTransform& boneModelSpaceInOut,
                            const RangeLimits* range = HK_NULL);
    };
} }
