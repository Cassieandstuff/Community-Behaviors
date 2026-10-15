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

    struct hkMemoryRouter
    {
        char               m_pad[0x58];
        hkMemoryAllocator* m_heap;   ///< +0x58

        static HK_FORCE_INLINE hkMemoryRouter& getInstance() { return *static_cast<hkMemoryRouter*>(TlsGetValue(hkMemoryRouter_s_memoryRouter)); }
        HK_FORCE_INLINE hkMemoryAllocator& heap() { return *m_heap; }
    };

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
