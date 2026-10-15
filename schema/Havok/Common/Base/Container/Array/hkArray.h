/// @file
/// @brief hkArray and its out-of-line growth helpers.
#pragma once

#include <Common/Base/Memory/hkMemory.h>

namespace CB { namespace schema
{
    struct hkArrayUtil
    {
        static void _reserve(hkMemoryAllocator& a, void* array, int reqCapacity, int sizeElem);
        /// @brief Grows a full array (pushBack's slow path).
        static void _reserveMore(hkMemoryAllocator& a, void* array, int sizeElem);
    };

    template <class T> struct hkArray
    {
        enum { CAPACITY_MASK = 0x3fffffff, DONT_DEALLOCATE_FLAG = int(0x80000000) };

        T*  m_data;
        int m_size;
        int m_capacityAndFlags;

        HK_FORCE_INLINE hkArray() : m_data(HK_NULL), m_size(0), m_capacityAndFlags(DONT_DEALLOCATE_FLAG) {}
        /// @brief Frees owned storage and leaves the array empty and non-owning.
        /// @warning Elements are not destroyed: correct only for trivially destructible element types so far.
        HK_FORCE_INLINE ~hkArray()
        {
            m_size = 0;
            if (m_capacityAndFlags >= 0) {
                hkMemoryAllocator& a = hkContainerHeapAllocator::s_alloc;
                a.bufFree(m_data, getCapacity() * int(sizeof(T)));
            }
            m_data             = HK_NULL;
            m_capacityAndFlags = DONT_DEALLOCATE_FLAG;
        }

        HK_FORCE_INLINE int      getSize() const { return m_size; }
        HK_FORCE_INLINE int      getCapacity() const { return m_capacityAndFlags & CAPACITY_MASK; }
        HK_FORCE_INLINE T&       operator[](int i) { return m_data[i]; }
        HK_FORCE_INLINE const T& operator[](int i) const { return m_data[i]; }

        HK_FORCE_INLINE void setSize(int n)
        {
            reserve(n);
            m_size = n;
        }
        HK_FORCE_INLINE void removeAtAndCopy(int index)
        {
            m_size--;
            hkMemUtil::memCpy8(m_data + index, m_data + index + 1, (m_size - index) * int(sizeof(T)));
        }
        HK_FORCE_INLINE void reserve(int n)
        {
            if (getCapacity() < n) {
                const int cap2    = 2 * getCapacity();
                const int newSize = (n < cap2) ? cap2 : n;
                hkArrayUtil::_reserve(hkContainerHeapAllocator::s_alloc, this, newSize, sizeof(T));
            }
        }
        /// @note Constructs through the global placement new: vanilla null-checks the slot.
        HK_FORCE_INLINE void pushBack(const T& t)
        {
            if (m_size == getCapacity()) hkArrayUtil::_reserveMore(hkContainerHeapAllocator::s_alloc, this, sizeof(T));
            new (m_data + m_size) T(t);
            m_size++;
        }
    };
} }
