/// @file
/// @brief hkBitField: a bit array over hkArray<hkUint32> words.
#pragma once

#include <Common/Base/Container/Array/hkArray.h>

namespace CB { namespace schema
{
    struct hkBitField
    {
        hkArray<hkUint32> m_words;
        int               m_numBits;

        /// @brief Wraps caller-owned `storage` (never freed) and clears it.
        HK_FORCE_INLINE hkBitField(int numBits, hkUint32* storage)
        {
            m_numBits                  = numBits;
            const int numWords         = (numBits + 31) >> 5;
            m_words.m_data             = storage;
            m_words.m_size             = numWords;
            m_words.m_capacityAndFlags = numWords | hkArray<hkUint32>::DONT_DEALLOCATE_FLAG;
            for (int i = 0; i < m_words.getSize(); i++) m_words[i] = 0;
        }
    };
} }
