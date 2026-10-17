/// @file
/// @brief hkLocalArray: an hkArray whose initial storage is a block of the thread's stack allocator.
#pragma once

#include <Common/Base/Container/Array/hkArray.h>

namespace CB { namespace schema
{
    /// @invariant Local arrays on one thread are destroyed in reverse order of construction (the allocator is a LIFO).
    /// @note Growing past the initial capacity moves the elements to the heap; the stack block is returned regardless.
    template <typename T> class hkLocalArray : public hkArray<T>
    {
    public:
        HK_FORCE_INLINE hkLocalArray(int n) : m_initialCapacity(n)
        {
            this->m_data             = n ? hkAllocateStack<T>(n) : HK_NULL;
            this->m_capacityAndFlags = n | hkArray<T>::DONT_DEALLOCATE_FLAG;
            m_localMemory            = this->m_data;
        }
        HK_FORCE_INLINE ~hkLocalArray()
        {
            if (m_localMemory == this->m_data) this->m_size = 0;
            hkDeallocateStack<T>(m_localMemory, m_initialCapacity);
        }

        T*  m_localMemory;       ///< +0x10
        int m_initialCapacity;   ///< +0x18
    };
} }
