/// @file
/// @brief hkpProperty: a world object's keyed user property.
#pragma once

#include <Common/Base/Types/hkBaseTypes.h>

namespace CB { namespace schema
{
    /// @brief A property's value as Havok reflects it: one 64-bit word.
    /// @note hkpProperty keeps it as a raw hkUlong (matched code reads it as one).
    struct hkpPropertyValue
    {
        hkUlong m_data;   ///< +0x00
    };

    /// @brief A world object's user property: a key and a 64-bit value.
    /// @note 0x10 bytes (schema/physics/hkpProperty.yaml); m_value is the hkpPropertyValue union, held raw.
    struct hkpProperty
    {
        hkUint32 m_key;                ///< +0x00
        hkUint32 m_alignmentPadding;   ///< +0x04
        hkUlong  m_value;              ///< +0x08 — hkpPropertyValue

        HK_FORCE_INLINE void* getPtr() const { return reinterpret_cast<void*>(m_value); }
    };
} }
