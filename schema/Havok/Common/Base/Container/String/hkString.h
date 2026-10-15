/// @file
/// @brief hkStringPtr, hkStringBuf and the hkString utilities.
#pragma once

#include <Common/Base/Memory/hkMemory.h>

namespace CB { namespace schema
{
    /// @note A `class`, as in Havok: the class-key is part of every mangled name that takes one.
    class hkStringPtr
    {
    public:
        const char* m_stringAndFlag;   ///< low bit = owned

        /// @note Out of line in vanilla (0x140a8d2f0, stores null): the node default constructors call it.
        hkStringPtr();
        hkStringPtr(hkFinishLoadedObjectFlag flag);
        /// @note The flag's meaning is unknown; the node clone copies pass false.
        hkStringPtr(const hkStringPtr& other, bool flag);
        ~hkStringPtr();
        hkStringPtr& operator=(const char* s);
        /// @brief Formats into a temporary buffer, then assigns it.
        void printf(const char* fmt, ...);

        HK_FORCE_INLINE const char* cString() const { return reinterpret_cast<const char*>(reinterpret_cast<hkUlong>(m_stringAndFlag) & ~hkUlong(1)); }
    };

    struct hkStringBuf
    {
        char* m_data;
        int   m_size;
        int   m_capacityAndFlags;
        char  m_storage[128];

        HK_FORCE_INLINE hkStringBuf()
        {
            m_data             = m_storage;
            m_size             = 1;
            m_capacityAndFlags = int(0x80000000) | 128;
            m_storage[0]       = 0;
        }
        /// @note Out of line in vanilla; a null `s` gives the empty string.
        hkStringBuf(const char* s);
        hkStringBuf(const hkStringPtr& s);
        /// @note Resets to empty and non-owning after the free, like hkArray's destructor (vanilla keeps those stores
        ///       when the buffer dies inside a loop).
        HK_FORCE_INLINE ~hkStringBuf()
        {
            m_size = 0;
            if (m_capacityAndFlags >= 0) {
                hkMemoryAllocator& a = hkContainerTempAllocator::s_alloc;
                a.bufFree(m_data, m_capacityAndFlags & 0x3fffffff);
            }
            m_data             = HK_NULL;
            m_capacityAndFlags = int(0x80000000);
        }

        void printf(const char* fmt, ...);
        HK_FORCE_INLINE const char* cString() const { return m_data; }
    };

    struct hkString
    {
        static int  strCmp(const char* a, const char* b);
        static int  strNcmp(const char* a, const char* b, int n);
        static int  strLen(const char* s);
        /// @brief Whether `s` starts with `prefix` (case-sensitive; an empty prefix always matches).
        static hkBool beginsWith(const char* s, const char* prefix);
        /// @note Out of line: zero-extends `nbytes` and tail-jumps to memcpy.
        static void memCpy(void* dst, const void* src, int nbytes);
        static void memSet(void* dst, const int c, int nbytes);

        /// @brief Fills `numQuads` 16-byte blocks of `dst` with the 16 bytes at `value`.
        static HK_FORCE_INLINE void memSet16(void* dst, const void* value, int numQuads)
        {
            const hkUint32* src = static_cast<const hkUint32*>(value);
            hkUint32*       d   = static_cast<hkUint32*>(dst);
            for (int i = 0; i < numQuads; i++) {
                *d++ = src[0];
                *d++ = src[1];
                *d++ = src[2];
                *d++ = src[3];
            }
        }
    };
} }
