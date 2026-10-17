/// @file
/// @brief The memory layer: allocators, the per-thread memory router, the container allocators and hkMemUtil.
#pragma once

#include <Common/Base/Types/hkBaseTypes.h>

namespace CB { namespace schema
{
    /// @note Vtable order is the engine's: slots 1–4 are blockAlloc, blockFree, bufAlloc, bufFree.
    struct hkMemoryAllocator
    {
        virtual ~hkMemoryAllocator();
        virtual void* blockAlloc(int numBytes);
        virtual void  blockFree(void* p, int numBytes);
        virtual void* bufAlloc(int& reqNumBytesInOut);
        virtual void  bufFree(void* p, int numBytes);
    };

    extern DWORD hkMemoryRouter_s_memoryRouter;   ///< the router's TLS index

    /// @brief The per-thread stack allocator: slabs handed out and returned strictly last-in first-out.
    struct hkLifoAllocator : hkMemoryAllocator
    {
        void*              m_impl;              ///< +0x08
        int                m_slabSize;          ///< +0x10
        void*              m_cur;               ///< +0x18
        void*              m_end;               ///< +0x20
        void*              m_firstNonLifoEnd;   ///< +0x28
        void*              m_cachedEmptySlab;   ///< +0x30
        hkMemoryAllocator* m_slabAllocator;     ///< +0x38
        hkMemoryAllocator* m_internalAllocator; ///< +0x40
        hkMemoryAllocator* m_externalAllocator; ///< +0x48

        void* allocateFromNewSlab(int numBytes);
        void  slowBlockFree(void* p, int numBytes);

        /// @pre `numBytes` is a multiple of 16 (hkAllocateStack passes multiples of 128).
        HK_FORCE_INLINE void* fastBlockAlloc(int numBytes)
        {
            void* p   = m_cur;
            void* end = static_cast<char*>(p) + numBytes;
            if (numBytes <= m_slabSize && end <= m_end) {
                m_cur = end;
                return p;
            }
            return allocateFromNewSlab(numBytes);
        }
        /// @note Frees in place only the top of the current slab; anything else goes to slowBlockFree.
        HK_FORCE_INLINE void fastBlockFree(void* p, int numBytesIn)
        {
            const int numBytes = (numBytesIn + 15) & ~15;
            if (numBytesIn <= m_slabSize && static_cast<char*>(p) + numBytes == m_cur && m_firstNonLifoEnd != p) {
                m_cur = p;
            } else {
                slowBlockFree(p, numBytes);
            }
        }
    };

    struct hkMemoryRouter
    {
        hkLifoAllocator    m_stack;   ///< +0x00
        hkMemoryAllocator* m_temp;    ///< +0x50
        hkMemoryAllocator* m_heap;    ///< +0x58

        static HK_FORCE_INLINE hkMemoryRouter& getInstance() { return *static_cast<hkMemoryRouter*>(TlsGetValue(hkMemoryRouter_s_memoryRouter)); }
        HK_FORCE_INLINE hkLifoAllocator&   stack() { return m_stack; }
        HK_FORCE_INLINE hkMemoryAllocator& heap() { return *m_heap; }

        /// @brief A block that remembers its own size (so easyFree needs none).
        /// @note Out of line in vanilla (0x140a89640).
        static void* easyAlloc(hkMemoryAllocator& b, int nbytes);
        /// @brief Frees an easyAlloc block.
        /// @note Out of line in vanilla (0x140a896a0).
        static void  easyFree(hkMemoryAllocator& b, void* p);
        /// @brief A block aligned to `align` bytes (the alignment padding recorded in front of it).
        /// @note Out of line in vanilla (0x140a895a0).
        static void* alignedAlloc(hkMemoryAllocator& b, int nbytes, int align);
        /// @brief Frees an alignedAlloc block.
        /// @note Out of line in vanilla (0x140a89610).
        static void  alignedFree(hkMemoryAllocator& b, void* p);
    };
    static_assert(sizeof(hkLifoAllocator) == 0x50, "hkLifoAllocator size");

    /// @brief `n` T's from the thread's heap, as a self-sized (easyAlloc) block.
    template <typename T>
    HK_FORCE_INLINE T* hkAllocate(int n)
    {
        return static_cast<T*>(hkMemoryRouter::easyAlloc(hkMemoryRouter::getInstance().heap(), n * int(sizeof(T))));
    }
    /// @brief Frees an hkAllocate block.
    template <typename T>
    HK_FORCE_INLINE void hkDeallocate(T* p)
    {
        hkMemoryRouter::easyFree(hkMemoryRouter::getInstance().heap(), p);
    }
    /// @brief `n` T's from the thread's heap, aligned to `alignment` bytes.
    template <typename T>
    HK_FORCE_INLINE T* hkAlignedAllocate(int alignment, int n)
    {
        return static_cast<T*>(hkMemoryRouter::alignedAlloc(hkMemoryRouter::getInstance().heap(), n * int(sizeof(T)), alignment));
    }
    /// @brief Frees an hkAlignedAllocate block.
    template <typename T>
    HK_FORCE_INLINE void hkAlignedDeallocate(T* p)
    {
        hkMemoryRouter::alignedFree(hkMemoryRouter::getInstance().heap(), p);
    }
    /// @brief Frees a heap block of `n` T's (blockFree with the size the caller knows).
    template <typename T>
    HK_FORCE_INLINE void hkDeallocateChunk(T* p, int n)
    {
        hkMemoryRouter::getInstance().heap().blockFree(p, n * int(sizeof(T)));
    }

    /// @brief `n` T's on the thread's stack allocator, the block rounded up to 128 bytes.
    /// @note The router fetch is its own statement: vanilla calls TlsGetValue before computing the size.
    template <typename T>
    HK_FORCE_INLINE T* hkAllocateStack(int n)
    {
        hkLifoAllocator& stack = hkMemoryRouter::getInstance().stack();
        return static_cast<T*>(stack.fastBlockAlloc((n * int(sizeof(T)) + 127) & ~127));
    }
    /// @pre `p` is the most recent live hkAllocateStack block of `n` T's (else it takes the slow path).
    template <typename T>
    HK_FORCE_INLINE void hkDeallocateStack(T* p, int n)
    {
        hkMemoryRouter::getInstance().stack().fastBlockFree(p, (n * int(sizeof(T)) + 127) & ~127);
    }

    struct hkContainerHeapAllocator
    {
        struct Allocator : hkMemoryAllocator {};
        static Allocator s_alloc;
    };
    struct hkContainerTempAllocator
    {
        struct Allocator : hkMemoryAllocator {};
        static Allocator s_alloc;
    };

    struct hkMemUtil
    {
        /// @brief Copies one `ELEMENT_SIZE`-byte element, a qword at a time.
        /// @pre `ELEMENT_SIZE` is a multiple of 8 and both pointers are 8-aligned.
        template <int ELEMENT_SIZE> static HK_FORCE_INLINE void memCpyOne(void* dst, const void* src)
        {
            unsigned __int64* d     = static_cast<unsigned __int64*>(dst);
            const __int64     delta = static_cast<const char*>(src) - static_cast<const char*>(dst);
            for (int i = 0; i < ELEMENT_SIZE; i += 8) {
                *d = *reinterpret_cast<const unsigned __int64*>(reinterpret_cast<const char*>(d) + delta);
                d++;
            }
        }
        /// @brief Havok's word-wise copy: ceil(nbytes / 8) qword moves.
        /// @note Vanilla's one-pointer-plus-delta loop form is not reproduced under cl 19.00.24247 (compiler-build
        ///       variance; the callers come out VARIANT).
        static HK_FORCE_INLINE void memCpy8(void* dst, const void* src, int nbytes)
        {
            unsigned __int64*       d = static_cast<unsigned __int64*>(dst);
            const unsigned __int64* s = static_cast<const unsigned __int64*>(src);
            for (int i = 0; i < nbytes; i += 8) *d++ = *s++;
        }
    };

    /// @brief One qword: a single move.
    /// @note Elements narrower than a qword copy as one element (vanilla: a word move for an hkArray<hkUint16>::removeAt).
    template <> HK_FORCE_INLINE void hkMemUtil::memCpyOne<2>(void* dst, const void* src)
    {
        *static_cast<hkUint16*>(dst) = *static_cast<const hkUint16*>(src);
    }
    template <> HK_FORCE_INLINE void hkMemUtil::memCpyOne<4>(void* dst, const void* src)
    {
        *static_cast<hkUint32*>(dst) = *static_cast<const hkUint32*>(src);
    }
    template <> HK_FORCE_INLINE void hkMemUtil::memCpyOne<8>(void* dst, const void* src)
    {
        *static_cast<unsigned __int64*>(dst) = *static_cast<const unsigned __int64*>(src);
    }
} }
