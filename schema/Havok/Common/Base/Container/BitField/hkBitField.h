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

        /// @brief Wraps caller-owned `storage` (never freed) and sets every bit of it to `initialValue` (0 or 1).
        HK_FORCE_INLINE hkBitField(int numBits, hkUint32* storage, int initialValue)
        {
            m_numBits                  = numBits;
            const int numWords         = (numBits + 31) >> 5;
            m_words.m_data             = storage;
            m_words.m_size             = numWords;
            m_words.m_capacityAndFlags = numWords | hkArray<hkUint32>::DONT_DEALLOCATE_FLAG;
            const hkUint32 fill        = initialValue ? 0xffffffffu : 0u;
            for (int i = 0; i < m_words.getSize(); i++) m_words[i] = fill;
        }

        /// @brief Wraps caller-owned `storage` (never freed) as it is: the bits are not touched.
        /// @note Signature provisional (BSBoneSwitchGeneratorUtils reads the generator outputs' partition bits through it).
        HK_FORCE_INLINE hkBitField(hkUint32* storage, int numBits)
        {
            m_numBits                  = numBits;
            const int numWords         = (numBits + 31) >> 5;
            m_words.m_data             = storage;
            m_words.m_size             = numWords;
            m_words.m_capacityAndFlags = numWords | hkArray<hkUint32>::DONT_DEALLOCATE_FLAG;
        }

        /// @return Bit `index`, as 0 or 1.
        HK_FORCE_INLINE int  get(int index) const { return (m_words[index >> 5] >> (index & 31)) & 1; }
        HK_FORCE_INLINE void set(int index) { m_words[index >> 5] |= (1 << (index & 31)); }
        /// @brief Sets bit `index` to `value` (0 or 1) without branching.
        HK_FORCE_INLINE void assign(int index, int value)
        {
            const hkUint32 mask = 1 << (index & 31);
            const int      word = index >> 5;
            m_words[word]       = (hkUint32(~(value - 1)) & mask) | (m_words[word] & ~mask);
        }
    };
} }
