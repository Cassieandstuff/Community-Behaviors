/// @file
/// @brief hkStringMap: a map from C strings (compared by content) to word-sized values, over hkCachedHashMap.
#pragma once

#include <Common/Base/Container/PointerMap/hkPointerMap.h>

namespace CB { namespace schema
{
    /// @brief Hashes and compares keys as C strings (case-sensitive).
    struct hkStringMapOperations {};

    /// @note Out of line in vanilla; the keys are not copied (the caller keeps them alive). The constructor allocates
    ///       16 empty slots up front.
    template <typename Operations> struct hkCachedHashMap
    {
        void* m_elem;       ///< +0x00 — (key, hash, value) triples
        int   m_numElems;   ///< +0x08
        int   m_hashMod;    ///< +0x0c — capacity - 1
        Operations m_ops;   ///< +0x10

        hkCachedHashMap(Operations ops = Operations());
        ~hkCachedHashMap();

        void     insert(hkUlong key, hkUlong val);
        hkResult get(hkUlong key, hkUlong* out) const;
    };

    template <typename V> struct hkStringMap
    {
        hkCachedHashMap<hkStringMapOperations> m_map;

        HK_FORCE_INLINE void insert(const char* key, V val) { m_map.insert(hkUlong(key), hkUlong(val)); }
        HK_FORCE_INLINE hkResult get(const char* key, V* out) const
        {
            hkUlong        tmp;
            const hkResult res = m_map.get(hkUlong(key), &tmp);
            if (res == HK_SUCCESS) *out = V(tmp);
            return res;
        }
    };
} }
