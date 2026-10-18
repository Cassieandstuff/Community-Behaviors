/// @file
/// @brief NiMatrix3: Gamebryo's 3x3 rotation matrix.
/// @note Not Havok: declared only as far as the character controller's code uses it.
#pragma once

#include <Common/Base/hkBase.h>

namespace CB { namespace schema
{
    /// @brief A rotation, row-major (Havok's hkRotation stores the same matrix by columns).
    /// @note 0x24 bytes.
    class NiMatrix3
    {
    public:
        float m_entry[3][3];   ///< +0x00

        /// @brief The rotation by `z` about z, then `x` about x, then `y` about y (radians).
        /// @note Out of line in vanilla (0x140d1af40).
        void EulerAnglesToAxesZXY(float z, float x, float y);
    };
} }
