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

    /// @note 0x40 bytes (vanilla allocates it so).
    class hkbVariableValueSet : public hkReferencedObject
    {
    public:
        hkArray<hkbVariableValue>    m_wordVariableValues;      ///< +0x10
        hkArray<hkVector4>           m_quadVariableValues;      ///< +0x20
        hkArray<hkReferencedObject*> m_variantVariableValues;   ///< +0x30

        /// @brief Copies every value (the variant ones referenced).
        /// @note Out of line in vanilla (0x140ac1dd0).
        hkbVariableValueSet& operator=(const hkbVariableValueSet& other);
    };
} }
