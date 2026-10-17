/// @file
/// @brief hkStepInfo: one simulation step's time span.
#pragma once

#include <Common/Base/Types/hkBaseTypes.h>

namespace CB { namespace schema
{
    /// @note 0x10 bytes.
    struct hkStepInfo
    {
        hkTime m_startTime;      ///< +0x00
        hkTime m_endTime;        ///< +0x04
        hkReal m_deltaTime;      ///< +0x08
        hkReal m_invDeltaTime;   ///< +0x0c
    };
} }
