/// @file
/// @brief hkbHandleUtils: where a handle is in the world, and handle identity.
#pragma once

#include <Behavior/Utils/hkbHandle.h>

namespace CB { namespace schema
{
    /// @note Names provisional (struct and methods): three out-of-line functions compiled together (0x140b04070,
    ///       0x140b04440, 0x140b045b0) that take a handle first; hkbEvaluateHandleModifier and a scene modifier call them.
    struct hkbHandleUtils
    {
        /// @brief The handle's frame in world space: the local frame's transform composed with the rigid body's, or with
        ///        the character's animation bone in `poseLocal` (the identity for no handle).
        /// @note A null `worldFromModel` / `poseLocal` means the character's own.
        static void getWorldFromHandle(const hkbHandle& handle, hkTransform& worldFromHandle, const hkQsTransform* worldFromModel,
                                       const hkQsTransform* poseLocal, int numPoseLocal);
        /// @brief getWorldFromHandle for a rigid body, its motion extrapolated `timeStep` seconds ahead (taken under the
        ///        world lock).
        static void getExtrapolatedWorldFromHandle(const hkbHandle& handle, hkTransform& worldFromHandle, hkReal timeStep);
        /// @brief Whether both handles name the same target (frame, body, character and bone).
        static bool areEqual(const hkbHandle& a, const hkbHandle& b);
    };
} }
