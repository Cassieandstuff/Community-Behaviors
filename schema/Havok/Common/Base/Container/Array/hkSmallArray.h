/// @file
/// @brief hkSmallArray: an hkArray with 16-bit size and capacity.
#pragma once

#include <Common/Base/Types/hkBaseTypes.h>

namespace CB { namespace schema
{
    /// @note 0x10 bytes; the capacity's top bit is hkArray's DONT_DEALLOCATE flag.
    template <class T> class hkSmallArray
    {
    public:
        T*       m_data;               ///< +0x00
        hkUint16 m_size;               ///< +0x08
        hkUint16 m_capacityAndFlags;   ///< +0x0a

        HK_FORCE_INLINE int      getSize() const { return m_size; }
        HK_FORCE_INLINE T&       operator[](int i) { return m_data[i]; }
        HK_FORCE_INLINE const T& operator[](int i) const { return m_data[i]; }
        /// @brief The first index holding `t`, -1 if none.
        HK_FORCE_INLINE int indexOf(const T& t) const
        {
            for (int i = 0; i < m_size; i++) {
                if (m_data[i] == t) return i;
            }
            return -1;
        }
    };
} }
