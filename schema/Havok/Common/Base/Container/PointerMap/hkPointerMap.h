/// @file
/// @brief The hkPointerMap body (hkUlong → hkUlong) and the typed hkPointerMap over it.
#pragma once

#include <Common/Base/Memory/hkMemory.h>

namespace CB { namespace schema
{
    enum hkResult { HK_SUCCESS = 0, HK_FAILURE = 1 };

    /// @note Name provisional: the engine functions are pinned, the class they belong to (Havok's hkMap body over
    ///       hkUlong keys and values) is not identified by name yet.
    struct hkPointerMapStorage
    {
        void* m_elem;       ///< (key, value) pairs; key -1 = empty
        int   m_numElems;
        int   m_hashMod;    ///< capacity - 1

        HK_FORCE_INLINE hkPointerMapStorage() : m_elem(HK_NULL), m_numElems(0), m_hashMod(-1) {}
        /// @note Out of line in vanilla (and empty: the owner frees the storage with clearAndDeallocate first).
        ~hkPointerMapStorage();

        hkUlong  getWithDefault(hkUlong key, hkUlong def) const;
        hkResult get(hkUlong key, hkUlong* out) const;
        /// @return Nonzero when the key was new.
        int      insert(hkMemoryAllocator& alloc, hkUlong key, hkUlong val);
        void     clearAndDeallocate(hkMemoryAllocator& alloc);
    };

    /// @brief A map between pointer-sized keys and values, stored in the container heap.
    /// @note Allocated through the thread's heap with its static size; `new` is null-checked, as in vanilla.
    template <class K, class V> struct hkPointerMap
    {
        hkPointerMapStorage m_map;

        HK_FORCE_INLINE void* operator new(hkUlong nbytes) throw() { return hkMemoryRouter::getInstance().heap().blockAlloc(static_cast<int>(nbytes)); }
        HK_FORCE_INLINE void  operator delete(void* p, hkUlong nbytes) { hkMemoryRouter::getInstance().heap().blockFree(p, static_cast<int>(nbytes)); }

        HK_FORCE_INLINE hkPointerMap() {}
        HK_FORCE_INLINE ~hkPointerMap() { m_map.clearAndDeallocate(hkContainerHeapAllocator::s_alloc); }

        HK_FORCE_INLINE int      insert(K key, V val) { return m_map.insert(hkContainerHeapAllocator::s_alloc, hkUlong(key), hkUlong(val)); }
        HK_FORCE_INLINE V        getWithDefault(K key, V def) const { return V(m_map.getWithDefault(hkUlong(key), hkUlong(def))); }
        HK_FORCE_INLINE hkResult get(K key, V* out) const
        {
            hkUlong        tmp;
            const hkResult res = m_map.get(hkUlong(key), &tmp);
            if (res == HK_SUCCESS) *out = V(tmp);
            return res;
        }
    };
} }
