/// @file
/// @brief hkbFootIkGains: the gains foot IK blends with (the modifier's own, or the controls track's).
#pragma once

#include <Common/Base/hkBase.h>

namespace CB { namespace schema
{
    /// @note 0x30 bytes; the defaults are vanilla's (both modifiers' constructors store them).
    struct hkbFootIkGains
    {
        hkReal m_onOffGain;                    ///< +0x00
        hkReal m_groundAscendingGain;          ///< +0x04
        hkReal m_groundDescendingGain;         ///< +0x08
        hkReal m_footPlantedGain;              ///< +0x0c
        hkReal m_footRaisedGain;               ///< +0x10
        hkReal m_footUnlockGain;               ///< +0x14
        hkReal m_worldFromModelFeedbackGain;   ///< +0x18
        hkReal m_errorUpDownBias;              ///< +0x1c
        hkReal m_alignWorldFromModelGain;      ///< +0x20
        hkReal m_hipOrientationGain;           ///< +0x24
        hkReal m_maxKneeAngleDifference;       ///< +0x28
        hkReal m_ankleOrientationGain;         ///< +0x2c

        HK_FORCE_INLINE hkbFootIkGains()
        :   m_onOffGain(0.2f),
            m_groundAscendingGain(1.0f),
            m_groundDescendingGain(1.0f),
            m_footPlantedGain(1.0f),
            m_footRaisedGain(1.0f),
            m_footUnlockGain(1.0f),
            m_worldFromModelFeedbackGain(0.0f),
            m_errorUpDownBias(1.0f),
            m_alignWorldFromModelGain(0.0f),
            m_hipOrientationGain(0.0f),
            m_maxKneeAngleDifference(0.0f),
            m_ankleOrientationGain(0.5f)
        {
        }

        /// @brief The fraction of the way a gain moves its value this step: gain x timestep x 30, clamped to [0, 1].
        /// @note Name and owner provisional: a free-standing function (0x140ad6450, outside any class's unit) that only
        ///       foot IK calls (hkbFootIkModifier::modify and hkbFootIkDriver's step, once per gain).
        static hkReal computeGainFraction(hkReal gain, hkReal timestep);
    };

    /// @brief What hkbFootIkControlsModifier writes into the foot IK controls track.
    /// @note 0x30 bytes, 16-aligned (vanilla copies it with movaps).
    struct hkbFootIkControlData
    {
        __declspec(align(16)) hkbFootIkGains m_gains;   ///< +0x00
    };
} }
