/// @file
/// @brief hkQueue: a growable ring buffer.
#pragma once

#include <Common/Base/Memory/hkMemory.h>

namespace CB { namespace havok
{
    template <class T> struct hkQueue
    {
        T*  m_data;            ///< +0x00
        int m_capacity;        ///< +0x08
        int m_head;            ///< +0x0c
        int m_tail;            ///< +0x10
        int m_elementsInUse;   ///< +0x14

        /// @note Out of line (not `inline`): vanilla calls the hkbEvent instantiation (0x140ab74d0, 0x140ab7500) from
        ///       hkbBehaviorGraph::update.
        hkQueue();
        ~hkQueue();

        void setCapacity(int n);

        HK_FORCE_INLINE int getSize() const { return m_elementsInUse; }

        HK_FORCE_INLINE void increaseCapacity()
        {
            if (m_capacity == 0) setCapacity(8);
            else                 setCapacity(m_capacity * 2);
        }
        /// @pre The queue is not empty (no check, as in vanilla's callers).
        HK_FORCE_INLINE void dequeue(T& data)
        {
            data = m_data[m_head];
            if (++m_head == m_capacity) m_head = 0;
            m_elementsInUse--;
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

    template <class T> hkQueue<T>::hkQueue() : m_data(HK_NULL), m_capacity(0), m_head(0), m_tail(0), m_elementsInUse(0) {}

    /// @note The storage is a heap block of exactly m_capacity elements.
    template <class T> hkQueue<T>::~hkQueue()
    {
        const int capacity = m_capacity;
        if (capacity) {
            T* data = m_data;
            hkMemoryRouter::getInstance().heap().blockFree(data, capacity * int(sizeof(T)));
        }
    }
} }
