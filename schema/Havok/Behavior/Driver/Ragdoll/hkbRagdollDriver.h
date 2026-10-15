/// @file
/// @brief hkbRagdollDriver: only what the behavior nodes and hkbCharacter call.
#pragma once

#include <Common/Base/hkBase.h>

namespace CB { namespace schema
{
    class hkaRagdollInstance;

    /// @note Member names below +0x88 and the reset state are provisional (read off reset() and setRagdoll()).
    class hkbRagdollDriver : public hkReferencedObject
    {
    public:
        /// @note Copied into m_mode by reset().
        hkInt32             m_defaultMode;                  ///< +0x10
        char                m_pad14[0x20 - 0x14];
        /// @note Reset to identity by reset().
        hkQsTransform       m_lastWorldFromModel;           ///< +0x20
        char                m_pad50[0x88 - 0x50];
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
    };
} }
