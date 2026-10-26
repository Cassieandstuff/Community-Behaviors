/// @file
/// @brief hkbFootIkDriver: plants a character's feet on the ground (hkbFootIkSceneModifier runs it after generate).
#pragma once

#include <Common/Base/hkBase.h>

namespace CB { namespace havok
{
    class hkaFootPlacementIkSolver;
    struct hkbContext;
    struct hkbFootIkGains;
    struct hkbGeneratorOutput;

    /// @note 0x50 bytes. Member names are provisional (read off the constructor, the destructor and reset()).
    class hkbFootIkDriver : public hkReferencedObject
    {
    public:
        /// @brief One leg's solver and ground state (0x30 bytes).
        struct InternalLegData
        {
            hkVector4                 m_groundPosition;   ///< +0x00
            hkVector4                 m_vec10;            ///< +0x10
            /// @note Owned: deleted by reset() and the destructor.
            hkaFootPlacementIkSolver* m_footIkSolver;     ///< +0x20
            hkReal                    m_verticalError;    ///< +0x28
            hkBool                    m_hitSomething;     ///< +0x2c
            hkBool                    m_isPlantedMS;      ///< +0x2d
        };

        hkArray<InternalLegData> m_internalLegData;           ///< +0x10
        hkVector4                m_vec20;                     ///< +0x20, starts zero
        hkQuaternion             m_rotation30;                ///< +0x30, starts identity
        hkReal                   m_worldFromModelFeedback;    ///< +0x40
        hkReal                   m_real44;                    ///< +0x44
        hkBool                   m_isSetUp;                   ///< +0x48
        hkBool                   m_isGroundPositionValid;     ///< +0x49
        hkBool                   m_bool4A;                    ///< +0x4a
        hkReal                   m_real4C;                    ///< +0x4c

        hkbFootIkDriver();
        /// @brief Deletes every leg's solver.
        ~hkbFootIkDriver();

        /// @brief Drops every leg's solver and the ground state (hkbCharacter::deactivate).
        /// @note Out of line in vanilla (0x140ab9df0).
        void reset();
        /// @brief Runs foot IK on `generatorOutput`'s pose.
        /// @note Name provisional; out of line in vanilla (0x140ab7f30), called by hkbFootIkSceneModifier.
        void doFootIk(const hkbContext& context, hkbGeneratorOutput& generatorOutput);
        /// @note Out of line in vanilla (0x140ab9ec0); hkbFootIkControlsModifier reads the legs through it.
        const hkArray<InternalLegData>& getInternalLegData() const;
        /// @brief Turns the world-from-model's up toward the ground's normal under the legs, by
        ///        `gains.m_alignWorldFromModelGain`: the turn is kept in m_rotation30 and pre-multiplied onto the output's
        ///        world-from-model rotation.
        /// @note Name provisional; out of line in vanilla (0x140aba6c0), called by doFootIk while two or more legs have
        ///       valid ground positions. It turns only when the gain is positive and the normal differs from the model's
        ///       up by more than 0.001 in some component.
        void alignWorldFromModelUpWithGroundNormal(const hkbContext& context, const hkVector4& upWS, hkbGeneratorOutput& generatorOutput,
                                                   const hkbFootIkGains& gains);
        /// @brief The legs' vertical errors blended between the largest and the smallest:
        ///        (1 - errorUpDownBias) x max + errorUpDownBias x min.
        /// @note Name provisional; out of line in vanilla (0x140abc830). doFootIk scales it by the world-from-model
        ///       feedback gain into m_worldFromModelFeedback, applied to the world-from-model on the next frame.
        hkReal computeVerticalError(hkReal errorUpDownBias) const;
    };
} }
