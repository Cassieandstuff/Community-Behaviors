/// @file
/// @brief hkClass: a reflected class's description (name, parent, members).
#pragma once

#include <Common/Base/Types/hkBaseTypes.h>

namespace CB { namespace schema
{
    /// @note Opaque so far: only the out-of-line queries below are used.
    struct hkClass
    {
        /// @brief Whether this class is `other` or one of its parents (an `other` object can be used as one of these).
        hkBool isSuperClass(const hkClass& other) const;
    };
} }
