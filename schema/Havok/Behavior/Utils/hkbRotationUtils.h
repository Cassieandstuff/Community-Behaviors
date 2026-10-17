/// @file
/// @brief hkbRotationUtils: turns a rotation so that one up axis moves toward another.
/// @note File and struct names provisional: vanilla compiles the one function (0x140ad5880) among the behavior graph's
///       own helpers (0x140ad4bb0-0x140ad5bxx), and only hkbGetUpModifier::modify calls it.
#pragma once

#include <Common/Base/hkBase.h>

namespace CB { namespace schema
{
    struct hkbRotationUtils
    {
        /// @brief Turns `currentRotation` so its rotated `currentUp` moves toward `targetRotation`'s rotated `targetUp`,
        ///        by `fraction` of the angle between them.
        /// @note Within `epsilon` of aligned, `rotationOut` = `currentRotation`; within `epsilon` of opposed (no defined
        ///       axis) it is `targetRotation`. Name and parameter names provisional.
        static void rotateToAlignUp(const hkQuaternion& targetRotation, const hkQuaternion& currentRotation, const hkVector4& targetUp,
                                    const hkVector4& currentUp, hkQuaternion& rotationOut, hkReal fraction, hkReal epsilon);
    };
} }
