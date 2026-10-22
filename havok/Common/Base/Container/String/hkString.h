/// @file
/// @brief hkStringPtr, hkStringBuf and the hkString utilities.
#pragma once

#include <Common/Base/Memory/hkMemory.h>

namespace CB { namespace havok
{
    /// @note A `class`, as in Havok: the class-key is part of every mangled name that takes one.
    class hkStringPtr
    {
    public:
        const char* m_stringAndFlag;   ///< low bit = owned

        /// @note Out of line in vanilla (0x140a8d2f0, stores null): the node default constructors call it.
        hkStringPtr();
        hkStringPtr(hkFinishLoadedObjectFlag flag);
        /// @brief A copy of `s` (null stays null).
        hkStringPtr(const char* s);
        /// @note The flag's meaning is unknown; the node clone copies pass false.
        hkStringPtr(const hkStringPtr& other, bool flag);
        /// @brief The plain copy (hkbSenseHandleModifier's clone copy calls it).
        hkStringPtr(const hkStringPtr& other);
        ~hkStringPtr();
        hkStringPtr& operator=(const char* s);
        hkStringPtr& operator=(const hkStringPtr& other);
        /// @brief Formats into a temporary buffer, then assigns it.
        void printf(const char* fmt, ...);
        /// @note Out of line in vanilla (0x140a8d480): 0 for the null string, else hkString::strLen.
        int  getLength() const;

        HK_FORCE_INLINE const char* cString() const { return reinterpret_cast<const char*>(reinterpret_cast<hkUlong>(m_stringAndFlag) & ~hkUlong(1)); }
    };

    struct hkStringBuf
    {
        char* m_data;
        int   m_size;
        int   m_capacityAndFlags;
        char  m_storage[128];

        /// @note The capacity is stored before the size, as vanilla does everywhere.
        HK_FORCE_INLINE hkStringBuf()
        {
            m_data             = m_storage;
            m_capacityAndFlags = int(0x80000000) | 128;
            m_size             = 1;
            m_storage[0]       = 0;
        }
        /// @note Out of line in vanilla; a null `s` gives the empty string.
        hkStringBuf(const char* s);
        hkStringBuf(const hkStringPtr& s);
        /// @brief The first `len` characters of `s`.
        hkStringBuf(const char* s, int len);
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

        enum ReplaceType { REPLACE_ONE = 0, REPLACE_ALL = 1 };

        void printf(const char* fmt, ...);
        /// @brief Appends `s`.
        hkStringBuf& operator+=(const char* s);
        /// @brief The index of the first `c` / `s` at or after `startIndex` (before `endIndex`), else -1.
        int  indexOf(char c, int startIndex = 0, int endIndex = 0x7fffffff) const;
        int  indexOf(const char* s, int startIndex = 0, int endIndex = 0x7fffffff) const;
        /// @brief Replaces `from` with `to` (the first or every occurrence).
        void replace(char from, char to, ReplaceType rt = REPLACE_ALL);
        /// @brief Drops the first `n` characters.
        void chompStart(int n);
        /// @brief Keeps the `length` characters from `startOffset`.
        void slice(int startOffset, int length);
        HK_FORCE_INLINE int getLength() const { return m_size - 1; }
        HK_FORCE_INLINE const char* cString() const { return m_data; }
    };

    struct hkString
    {
        static int  strCmp(const char* a, const char* b);
        static int  strNcmp(const char* a, const char* b, int n);
        static int  strLen(const char* s);
        /// @brief Formats into `buf` (no size limit).
        static int  sprintf(char* buf, const char* fmt, ...);
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
        /// @brief Copies `numQuads` 16-byte blocks from `src` to `dst`, a 4-byte word at a time.
        /// @note Each block is read whole before it is written (four loads, then four stores), as vanilla does.
        static HK_FORCE_INLINE void memCpy16(void* dst, const void* src, int numQuads)
        {
            const hkUint32* s = static_cast<const hkUint32*>(src);
            hkUint32*       d = static_cast<hkUint32*>(dst);
            for (int i = 0; i < numQuads; i++) {
                const hkUint32 a = s[0];
                const hkUint32 b = s[1];
                const hkUint32 c = s[2];
                const hkUint32 e = s[3];
                d[0]             = a;
                d[1]             = b;
                d[2]             = c;
                d[3]             = e;
                d += 4;
                s += 4;
            }
        }
        /// @brief Copies `numWords` 4-byte words from `src` to `dst`, each read just before it is written.
        static HK_FORCE_INLINE void memCpy4(void* dst, const void* src, int numWords)
        {
            const hkUint32* s = static_cast<const hkUint32*>(src);
            hkUint32*       d = static_cast<hkUint32*>(dst);
            for (int i = 0; i < numWords; i++) d[i] = s[i];
        }
        /// @brief Fills `numWords` 4-byte words of `dst` with `value`.
        /// @note Counted down from numWords - 1, as vanilla tests it (`lea eax,[n-1]; test; js` before the rep stosd).
        static HK_FORCE_INLINE void memSet4(void* dst, const int value, int numWords)
        {
            hkUint32* d = static_cast<hkUint32*>(dst);
            for (int i = numWords - 1; i >= 0; i--) *d++ = value;
        }
    };
} }
