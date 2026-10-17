/// @file
/// @brief Setting: one of the engine's INI settings (a "bName:Section" entry) and the ones the character
///        controller reads.
/// @note Not Havok: declared only as far as the character controller's code reads it.
#pragma once

#include <Common/Base/hkBase.h>

namespace CB { namespace schema
{
    /// @note 0x18 bytes.
    class Setting
    {
    public:
        virtual ~Setting();

        /// @brief Typed by the name's first letter (b bool, f float, i int, u unsigned, s string).
        union Value
        {
            bool        b;
            float       f;
            hkInt32     i;
            hkUint32    u;
            const char* s;
        };

        Value       m_value;   ///< +0x08
        const char* m_name;    ///< +0x10
    };

    /// @brief bUseCharRBExtrapolation:HAVOK — read a rigid-body character's transform at the world's current time
    ///        (interpolated within the step) rather than as of the last step.
    extern Setting bUseCharRBExtrapolation_HAVOK;
    /// @brief bUseUnsupportedCast:HAVOK — on losing support, cast down fUnsupportCastLength:HAVOK before leaving the
    ///        ground (bhkCharacterStateOnGround::Simulate).
    extern Setting bUseUnsupportedCast_HAVOK;
    /// @brief fUnsupportCastLength:HAVOK — that cast's length, in Havok units.
    extern Setting fUnsupportCastLength_HAVOK;
    /// @brief fUnsupportedGravMult:HAVOK — the gravity multiplier while the cast holds an unsupported character on the
    ///        ground.
    extern Setting fUnsupportedGravMult_HAVOK;
    /// @brief fInAirFallingCharGravityMult:HAVOK — the gravity multiplier once an airborne character is below the
    ///        highest point it reached.
    extern Setting fInAirFallingCharGravityMult_HAVOK;
} }
