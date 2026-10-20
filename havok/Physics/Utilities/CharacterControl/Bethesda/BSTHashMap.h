/// @file
/// @brief BSTHashMap: Bethesda's open hash map (a scatter table of 0x40-byte entries here).
/// @note Not Havok: declared only as far as the character controller's code destroys one.
#pragma once

#include <Common/Base/hkBase.h>

namespace CB { namespace havok
{
    /// @note 0x28 bytes: capacity +0x04, free +0x08, good +0x0c, sentinel +0x10, the entries +0x20.
    class BSTScatterTableBase
    {
    public:
        typedef void (*EntryCallback)(void* entry, void* key);

        /// @brief Empties the table, calling `callback` on every occupied entry first; `freeEntries` also frees the
        ///        entry block.
        /// @note Name provisional; out of line in vanilla (per key/value type).
        void Clear(EntryCallback callback, bool freeEntries);

        hkUint8 m_pad00[0x28];
    };

    /// @note 0x30 bytes.
    template <class Key, class Value> class BSTHashMap
    {
    public:
        HK_FORCE_INLINE ~BSTHashMap() { m_table.Clear(&DestroyEntry, true); }

        /// @brief The per-entry callback the destructor passes (nothing to do: the table releases the key itself).
        static void DestroyEntry(void*, void*) {}

        hkUint8             m_pad00[0x08];
        BSTScatterTableBase m_table;   ///< +0x08
    };
} }
