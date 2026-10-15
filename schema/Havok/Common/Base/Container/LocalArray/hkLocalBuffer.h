/// @file
/// @brief hkLocalBuffer: a scoped scratch array on the thread's stack allocator.
#pragma once

#include <Common/Base/Memory/hkMemory.h>

namespace CB { namespace schema
{
    /// @warning Elements are neither constructed nor destroyed: plain data only.
    /// @invariant Buffers on one thread are destroyed in reverse order of construction (the allocator is a LIFO).
    template <typename T> class hkLocalBuffer
    {
    public:
        HK_FORCE_INLINE hkLocalBuffer(int capacity) : m_capacity(capacity) { m_p = hkAllocateStack<T>(capacity); }
        HK_FORCE_INLINE ~hkLocalBuffer() { hkDeallocateStack<T>(m_p, m_capacity); }

        HK_FORCE_INLINE T&       operator[](int i) { return m_p[i]; }
        HK_FORCE_INLINE const T& operator[](int i) const { return m_p[i]; }

    private:
        T*  m_p;
        int m_capacity;
    };
} }
