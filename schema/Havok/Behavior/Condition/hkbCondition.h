/// @file
/// @brief hkbCondition: a test a state machine transition can be guarded by.
#pragma once

#include <Common/Base/hkBase.h>

namespace CB { namespace schema
{
    struct hkbContext;

    /// @note Declared only as far as the reimplemented code uses it (references); its virtuals are not declared yet.
    class hkbCondition : public hkReferencedObject
    {
    };
} }
