/// @file
/// @brief hkbCharacterControllerControlData: what a behavior graph asks of the character controller each frame.
#pragma once

#include <Common/Base/hkBase.h>

namespace CB { namespace havok
{
    /// @note 0x20 bytes: the element of hkbGeneratorOutput's TRACK_CHARACTER_CONTROLLER_CONTROL_DATA. Member names are
    ///       the exe's reflected strings; the layout is hkbCharacterControllerModifier's (+0x50).
    struct hkbCharacterControllerControlData
    {
        hkVector4 m_desiredVelocity;           ///< +0x00
        hkReal    m_verticalGain;              ///< +0x10
        hkReal    m_horizontalCatchUpGain;     ///< +0x14
        hkReal    m_maxVerticalSeparation;     ///< +0x18
        hkReal    m_maxHorizontalSeparation;   ///< +0x1c

        HK_FORCE_INLINE hkbCharacterControllerControlData()
        :   m_desiredVelocity(hkVector4::getZero()),
            m_verticalGain(1.0f),
            m_horizontalCatchUpGain(1.0f),
            m_maxVerticalSeparation(0.0f),
            m_maxHorizontalSeparation(0.0f)
        {
        }
    };
} }
