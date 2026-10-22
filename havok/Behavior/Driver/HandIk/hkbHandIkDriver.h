/// @file
/// @brief hkbHandIkDriver: moves a character's hands onto their targets (the hand IK scene modifiers run it).
#pragma once

#include <Behavior/Utils/hkbHandle.h>

namespace CB { namespace havok
{
    struct hkbContext;
    struct hkbGeneratorOutput;

    /// @note 0x28 bytes. Member names are provisional (read off the constructor, the destructor and the drive pass). The
    ///       destructor is implicit: vanilla's out-of-line copy stores no vtable of its own.
    class hkbHandIkDriver : public hkReferencedObject
    {
    public:
        /// @brief One hand's target and fade state (0xf0 bytes).
        /// @note Only the layout the destructor depends on is spelled out: two handles (destroyed in reverse order).
        struct InternalHandData
        {
            /// @note A second handle (its destructor runs); its role is not identified.
            hkbHandle    m_handle00;         ///< +0x00
            /// @brief What the hand is reaching for (cleared when the hand IK fades out).
            hkbHandle    m_handle;           ///< +0x30
            hkTransform  m_transform60;      ///< +0x60
            hkVector4    m_targetPosition;   ///< +0xa0
            hkQuaternion m_targetRotation;   ///< +0xb0
            hkVector4    m_targetNormal;     ///< +0xc0
            hkReal       m_onFraction;       ///< +0xd0
            hkReal       m_normalOnFraction; ///< +0xd4
            hkReal       m_fadeInDuration;   ///< +0xd8
            hkReal       m_fadeOutDuration;  ///< +0xdc
            hkReal       m_real0E0;          ///< +0xe0
            hkBool       m_bool0E4;          ///< +0xe4
        };

        hkArray<InternalHandData> m_internalHandData;   ///< +0x10
        /// @note Cleared by reset so the driver sets its hands up again.
        hkBool                    m_isSetUp;            ///< +0x20

        hkbHandIkDriver();

        /// @note Out of line in vanilla (0x140ae0830).
        void reset();
        /// @brief Runs hand IK on `generatorOutput`'s pose; `isFixup` selects the second (post-attachment) pass.
        /// @note Name and parameter names provisional; out of line in vanilla (0x140adf7f0), called by
        ///       hkbHandIkSceneModifier (isFixup false) and hkbHandIkFixupSceneModifier (true).
        void drive(const hkbContext& context, hkbGeneratorOutput& generatorOutput, hkReal timestep, bool isFixup);
    };
} }
