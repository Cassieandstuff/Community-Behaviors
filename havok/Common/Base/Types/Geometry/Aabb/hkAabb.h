/// @file
/// @brief hkAabb: an axis-aligned bounding box.
#pragma once

#include <Common/Base/Math/hkMath.h>

namespace CB { namespace havok
{
    /// @note 0x20 bytes. The empty box (min +HK_REAL_MAX, max -HK_REAL_MAX) vanilla starts from sits at 0x1417ed520.
    struct hkAabb
    {
        hkVector4 m_min;   ///< +0x00
        hkVector4 m_max;   ///< +0x10
    };
} }
