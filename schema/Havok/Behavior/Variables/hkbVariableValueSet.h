/// @file
/// @brief hkbVariableValueSet: a graph's variable values (word-sized ones by index).
#pragma once

#include <Common/Base/hkBase.h>

namespace CB { namespace schema
{
    /// @brief A word-sized variable value: bool and integers sign-extended to 32 bits, reals as their bits.
    struct hkbVariableValue
    {
        hkInt32 m_value;
    };

    class hkbVariableValueSet : public hkReferencedObject
    {
    public:
        hkArray<hkbVariableValue> m_wordVariableValues;   ///< +0x10
    };
} }
