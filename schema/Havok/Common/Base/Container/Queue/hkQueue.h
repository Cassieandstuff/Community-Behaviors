/// @file
/// @brief hkQueue: a growable ring buffer.
#pragma once

#include <Common/Base/Types/hkBaseTypes.h>

namespace CB { namespace schema
{
    template <class T> struct hkQueue
    {
        T*  m_data;            ///< +0x00
        int m_capacity;        ///< +0x08
        int m_head;            ///< +0x0c
        int m_tail;            ///< +0x10
        int m_elementsInUse;   ///< +0x14

        void setCapacity(int n);

        HK_FORCE_INLINE void increaseCapacity()
        {
            if (m_capacity == 0) setCapacity(8);
            else                 setCapacity(m_capacity * 2);
        }
        HK_FORCE_INLINE void enqueue(const T& element)
        {
            if (m_elementsInUse >= m_capacity) increaseCapacity();
            if (m_tail == m_capacity) m_tail = 0;
            m_data[m_tail] = element;
            m_tail++;
            m_elementsInUse++;
        }
    };
} }
