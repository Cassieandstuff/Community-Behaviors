/// @file
/// @brief hkbRagdollDriver: only what the behavior nodes and hkbCharacter call.
#pragma once

#include <Common/Base/hkBase.h>

namespace CB { namespace schema
{
    class hkaRagdollInstance;
    class hkpRigidBody;

    /// @note Member names below +0x88 and the reset state are provisional (read off reset() and setRagdoll()).
    class hkbRagdollDriver : public hkReferencedObject
    {
    public:
        /// @note Copied into m_mode by reset().
        hkInt32             m_defaultMode;                  ///< +0x10
        char                m_pad14[0x20 - 0x14];
        /// @note Reset to identity by reset().
        hkQsTransform       m_lastWorldFromModel;           ///< +0x20
        char                m_pad50[0x70 - 0x50];
        /// @brief Rigid bodies attached to the ragdoll (hkbAttachmentModifier adds the attachee's body).
        /// @note Name per the Havok SDK; the value stored is 1.
        hkPointerMap<hkpRigidBody*, int> m_attachedRigidBodyToIndexMap;   ///< +0x70
        char                m_pad80[0x88 - 0x80];
        hkaRagdollInstance* m_ragdoll;                      ///< +0x88
        char                m_pad90[0xa0 - 0x90];
        /// @brief An hkMemoryRouter::easyAlloc block reset() frees.
        void*               m_scratch;                      ///< +0xa0
        /// @brief A heap block of m_numLastPoseLocal transforms reset() frees.
        hkQsTransform*      m_lastPoseLocal;                ///< +0xa8
        hkInt32             m_numLastPoseLocal;             ///< +0xb0
        hkReal              m_timeRigidBodyControllerActive;   ///< +0xb4
        hkReal              m_timePoweredControllerActive;     ///< +0xb8
        hkInt32             m_counter;                      ///< +0xbc
        hkInt32             m_mode;                         ///< +0xc0
        /// @note Raised by setRagdoll and reset.
        hkBool              m_ragdollChanged;               ///< +0xc4
        hkBool              m_flagC5;                       ///< +0xc5
        char                m_padC6[0xc7 - 0xc6];
        hkBool              m_flagsC7[4];                   ///< +0xc7

        /// @note Out of line in vanilla (returns +0x88).
        hkaRagdollInstance* getRagdoll() const;
        /// @brief Returns the driver to its freshly-set-up state (hkbCharacter::deactivate).
        void reset();
        /// @brief Swaps in `ragdoll` (referenced), tearing down the old one's state.
        /// @note Out of line in vanilla (0x140aac7b0); reset() re-sets the current ragdoll through it.
        void setRagdoll(hkaRagdollInstance* ragdoll);
        /// @brief Frees the easyAlloc block at +0x90.
        /// @note Name provisional; out of line in vanilla (0x140aaff00).
        void freeRagdollPoseWS();
        /// @brief Registers `rigidBody` as attached, when it is one of the ragdoll's bodies.
        /// @note Name per the Havok SDK.
        void addAttachedRigidBody(hkpRigidBody* rigidBody);
        /// @note Name per the Havok SDK.
        void removeAttachedRigidBody(hkpRigidBody* rigidBody);
        /// @brief Copies `numTransforms` transforms and scales them by `scale` times one of two global unit scales
        ///        (`toWorldScale` picks which: presumably Skyrim's game-to-Havok unit conversion and its inverse).
        /// @note Name, owner and parameter names provisional; out of line in vanilla (0x140aaec40), called by the ragdoll
        ///       driver, hkbGetUpModifier and hkbPoseMatchingGenerator (which passes false and 1).
        static void copyAndScalePose(bool toWorldScale, int numTransforms, const hkQsTransform* poseIn, hkQsTransform* poseOut, hkReal scale);
    };
} }
