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
        /// @brief A map over caller-owned storage of `sizeInBytes` (DONT_DEALLOCATE: it is never freed by the map).
        /// @note Out of line in vanilla (0x140a8c620).
        hkPointerMapStorage(void* ptr, int sizeInBytes);
        /// @brief The storage a map of `numKeys` keys needs (a power of two of at least 8 slots, twice the keys).
        /// @note Out of line in vanilla (0x140a8c560).
        static int getSizeInBytesFor(int numKeys);
        /// @note Out of line in vanilla (and empty: the owner frees the storage with clearAndDeallocate first).
        ~hkPointerMapStorage();

        hkUlong  getWithDefault(hkUlong key, hkUlong def) const;
        hkResult get(hkUlong key, hkUlong* out) const;
        /// @return Nonzero when the key was new.
        int      insert(hkMemoryAllocator& alloc, hkUlong key, hkUlong val);
        void     clearAndDeallocate(hkMemoryAllocator& alloc);
        /// @brief Empties the map, keeping its storage.
        /// @note Out of line in vanilla (0x140a8c2b0).
        void     clear();
        /// @brief The bytes the map's storage holds.
        /// @note Name provisional; out of line in vanilla (0x140a8c4f0).
        int      getMemSize() const;

        /// @brief Iteration over the occupied slots: an iterator is a slot index (sign-extended into a pointer-sized
        ///        handle, as Havok's hkMap::Iterator), valid while <= m_hashMod.
        struct Pair
        {
            hkUlong m_key;
            hkUlong m_value;
        };
        typedef struct IteratorTag* Iterator;
        HK_FORCE_INLINE Iterator getIterator() const
        {
            int i;
            for (i = 0; i <= m_hashMod; i++) {
                if (static_cast<const Pair*>(m_elem)[i].m_key != hkUlong(-1)) break;
            }
            return reinterpret_cast<Iterator>(hkLong(i));
        }
        HK_FORCE_INLINE Iterator getNext(Iterator it) const
        {
            int i;
            for (i = int(hkLong(it)) + 1; i <= m_hashMod; i++) {
                if (static_cast<const Pair*>(m_elem)[i].m_key != hkUlong(-1)) break;
            }
            return reinterpret_cast<Iterator>(hkLong(i));
        }
        HK_FORCE_INLINE bool    isValid(Iterator it) const { return int(hkLong(it)) <= m_hashMod; }
        HK_FORCE_INLINE hkUlong getValue(Iterator it) const { return static_cast<const Pair*>(m_elem)[int(hkLong(it))].m_value; }
        /// @brief Where `key` is (an invalid iterator when absent).
        Iterator findKey(hkUlong key) const;
        /// @pre `it` is valid.
        void     remove(Iterator it);
        /// @return HK_FAILURE when the key was absent.
        hkResult remove(hkUlong key);
    };

    /// @brief A map between pointer-sized keys and values, stored in the container heap.
    /// @note Allocated through the thread's heap with its static size; `new` is null-checked, as in vanilla.
    template <class K, class V> struct hkPointerMap
    {
        hkPointerMapStorage m_map;

        HK_FORCE_INLINE void* operator new(hkUlong nbytes) throw() { return hkMemoryRouter::getInstance().heap().blockAlloc(static_cast<int>(nbytes)); }
        HK_FORCE_INLINE void  operator delete(void* p, hkUlong nbytes) { hkMemoryRouter::getInstance().heap().blockFree(p, static_cast<int>(nbytes)); }

        HK_FORCE_INLINE hkPointerMap() {}
        HK_FORCE_INLINE hkPointerMap(void* ptr, int sizeInBytes) : m_map(ptr, sizeInBytes) {}
        static HK_FORCE_INLINE int getSizeInBytesFor(int numKeys) { return hkPointerMapStorage::getSizeInBytesFor(numKeys); }
        HK_FORCE_INLINE void     clearAndDeallocate() { m_map.clearAndDeallocate(hkContainerHeapAllocator::s_alloc); }
        HK_FORCE_INLINE ~hkPointerMap() { m_map.clearAndDeallocate(hkContainerHeapAllocator::s_alloc); }

        HK_FORCE_INLINE void     clear() { m_map.clear(); }
        typedef hkPointerMapStorage::Iterator Iterator;
        HK_FORCE_INLINE Iterator getIterator() const { return m_map.getIterator(); }
        HK_FORCE_INLINE Iterator getNext(Iterator it) const { return m_map.getNext(it); }
        HK_FORCE_INLINE bool     isValid(Iterator it) const { return m_map.isValid(it); }
        HK_FORCE_INLINE V        getValue(Iterator it) const { return V(m_map.getValue(it)); }
        HK_FORCE_INLINE Iterator findKey(K key) const { return m_map.findKey(hkUlong(key)); }
        HK_FORCE_INLINE void     remove(Iterator it) { m_map.remove(it); }
        HK_FORCE_INLINE hkResult remove(K key) { return m_map.remove(hkUlong(key)); }
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
