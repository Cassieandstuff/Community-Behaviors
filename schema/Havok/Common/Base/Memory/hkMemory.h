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

    /// @brief The per-thread stack allocator: bump allocation within slabs, freed in reverse order.
    /// @note Only the members the inline fast paths touch are named; the rest of its 0x50 bytes is padding.
    struct hkLifoAllocator : hkMemoryAllocator
    {
        void* m_impl;              ///< +0x08
        int   m_slabSize;          ///< +0x10
        void* m_cur;               ///< +0x18
        void* m_end;               ///< +0x20
        void* m_firstNonLifoEnd;   ///< +0x28
        char  m_pad30[0x20];

        /// @brief fastBlockAlloc's slow path: the block does not fit the current slab.
        void* allocateFromNewSlab(int numBytes);
        /// @brief fastBlockFree's slow path: `p` is not the top of the current slab.
        void slowBlockFree(void* p, int numBytes);

        HK_FORCE_INLINE void* fastBlockAlloc(int numBytesIn)
        {
            const int numBytes = HK_NEXT_MULTIPLE_OF(16, numBytesIn);
            void*     p        = m_cur;
            void*     nextPtr  = static_cast<char*>(p) + numBytes;
            if (numBytes <= m_slabSize && nextPtr <= m_end) {
                m_cur = nextPtr;
                return p;
            } else {
                return allocateFromNewSlab(numBytes);
            }
        }
        HK_FORCE_INLINE void fastBlockFree(void* p, int numBytesIn)
        {
            const int numBytes = HK_NEXT_MULTIPLE_OF(16, numBytesIn);
            if (numBytesIn <= m_slabSize && static_cast<char*>(p) + numBytes == m_cur && m_firstNonLifoEnd != p) {
                m_cur = p;
            } else {
                slowBlockFree(p, numBytes);
            }
        }
    };
    static_assert(sizeof(hkLifoAllocator) == 0x50, "hkLifoAllocator size");

    extern DWORD hkMemoryRouter_s_memoryRouter;   ///< the router's TLS index

    struct hkMemoryRouter
    {
        hkLifoAllocator    m_stack;   ///< +0x00
        char               m_pad50[0x8];
        hkMemoryAllocator* m_heap;    ///< +0x58

        static HK_FORCE_INLINE hkMemoryRouter& getInstance() { return *static_cast<hkMemoryRouter*>(TlsGetValue(hkMemoryRouter_s_memoryRouter)); }
        HK_FORCE_INLINE hkLifoAllocator&   stack() { return m_stack; }
        HK_FORCE_INLINE hkMemoryAllocator& heap() { return *m_heap; }
    };

    /// @brief `n` T from the thread's stack allocator, rounded up to 128 bytes.
    template <typename T> HK_FORCE_INLINE T* hkAllocateStack(int n)
    {
        return static_cast<T*>(hkMemoryRouter::getInstance().stack().fastBlockAlloc(HK_NEXT_MULTIPLE_OF(128, n * int(sizeof(T)))));
    }
    /// @pre `p` is this thread's most recent live stack allocation, made with the same `n`.
    template <typename T> HK_FORCE_INLINE void hkDeallocateStack(T* p, int n)
    {
        hkMemoryRouter::getInstance().stack().fastBlockFree(p, HK_NEXT_MULTIPLE_OF(128, n * int(sizeof(T))));
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
} }
