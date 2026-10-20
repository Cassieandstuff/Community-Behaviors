/// @file
/// @brief hkbVariableInfo: a behavior variable's role and type.
#pragma once

#include <Common/Base/hkBase.h>

namespace CB { namespace havok
{
    struct hkbRoleAttribute
    {
        hkInt16 m_role;    ///< +0x0
        hkInt16 m_flags;   ///< +0x2
    };

    /// @note 6 bytes.
    struct hkbVariableInfo
    {
        enum VariableType
        {
            VARIABLE_TYPE_INVALID    = -1,
            VARIABLE_TYPE_BOOL       = 0,
            VARIABLE_TYPE_INT8       = 1,
            VARIABLE_TYPE_INT16      = 2,
            VARIABLE_TYPE_INT32      = 3,
            VARIABLE_TYPE_REAL       = 4,
            VARIABLE_TYPE_POINTER    = 5,
            VARIABLE_TYPE_VECTOR3    = 6,
            VARIABLE_TYPE_VECTOR4    = 7,
            VARIABLE_TYPE_QUATERNION = 8,
        };

        hkbRoleAttribute m_role;   ///< +0x0
        hkInt8           m_type;   ///< +0x4 — VariableType
    };
} }
