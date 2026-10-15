/// @file
/// @brief The hkPointerMap body (hkUlong → hkUlong).
#pragma once

#include <Common/Base/Types/hkBaseTypes.h>

namespace CB { namespace schema
{
    /// @note Name provisional: the engine function is pinned, the class it belongs to is not identified yet.
    struct hkPointerMapStorage
    {
        hkUlong getWithDefault(hkUlong key, hkUlong def) const;
    };
} }
