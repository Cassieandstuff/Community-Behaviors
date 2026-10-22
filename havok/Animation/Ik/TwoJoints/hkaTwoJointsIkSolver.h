/// @file
/// @brief hkaTwoJointsIkSolver: bends a two-joint chain (hip/knee, shoulder/elbow) so its end bone reaches a target.
/// @note Declarations only so far: solve is vanilla's (0x140a86640, timer list "TWOJOINTSOLVER"); hkbHandIkModifier and
///       hkaFootPlacementIkSolver call it. The Setup layout is read off solve's reads and matches the Havok SDK's.
#pragma once

#include <Animation/Rig/hkaPose.h>

namespace CB { namespace havok
{
    class hkaTwoJointsIkSolver
    {
    public:
        /// @note 0x90 bytes. Field names follow the Havok SDK; offsets confirmed against solve (bone indices, the
        ///       hinge axis +0x10, the hinge cosine clamp +0x20/+0x24, the three gains +0x28/+0x2c/+0x30, the
        ///       targets and offsets +0x40..+0x70, the two enforce flags +0x80/+0x81 it tests first).
        struct Setup
        {
            hkInt16      m_firstJointIdx;             ///< +0x00
            hkInt16      m_secondJointIdx;            ///< +0x02
            hkInt16      m_endBoneIdx;                ///< +0x04
            /// @brief -1 = none.
            hkInt16      m_firstJointTwistIdx;        ///< +0x06
            /// @brief -1 = none.
            hkInt16      m_secondJointTwistIdx;       ///< +0x08
            hkVector4    m_hingeAxisLS;               ///< +0x10
            hkReal       m_cosineMaxHingeAngle;       ///< +0x20, the clamp's lower bound
            hkReal       m_cosineMinHingeAngle;       ///< +0x24, the clamp's upper bound
            hkReal       m_firstJointIkGain;          ///< +0x28
            hkReal       m_secondJointIkGain;         ///< +0x2c
            hkReal       m_endJointIkGain;            ///< +0x30
            hkVector4    m_endTargetMS;               ///< +0x40
            hkQuaternion m_endTargetRotationMS;       ///< +0x50
            hkVector4    m_endBoneOffsetLS;           ///< +0x60
            hkQuaternion m_endBoneRotationOffsetLS;   ///< +0x70
            hkBool       m_enforceEndPosition;        ///< +0x80
            hkBool       m_enforceEndRotation;        ///< +0x81
        };

        /// @brief Solves the chain in `poseInOut` (model space) toward the setup's end target.
        /// @return True at once when neither the end position nor the end rotation is enforced.
        /// @note Out of line in vanilla (0x140a86640); the hkBool comes back through a hidden pointer.
        static hkBool solve(const Setup& setup, hkaPose& poseInOut);
    };
    static_assert(sizeof(hkaTwoJointsIkSolver::Setup) == 0x90, "hkaTwoJointsIkSolver::Setup size");
} }
