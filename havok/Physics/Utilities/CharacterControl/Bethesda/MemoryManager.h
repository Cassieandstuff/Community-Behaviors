/// @file
/// @brief MemoryManager: Bethesda's engine heap, and the class allocator its engine classes declare.
/// @note Not Havok: declared here only as far as the character controller's code calls it.
#pragma once

#include <Common/Base/hkBase.h>

namespace CB { namespace havok
{
    class ScrapHeap;

    /// @note Opaque; reached only through its singleton.
    class MemoryManager
    {
    public:
        /// @brief The engine heap, constructed on first use.
        /// @note The singleton's state reads 2 once it is constructed.
        static HK_FORCE_INLINE MemoryManager* GetSingleton()
        {
            if (s_singletonState != 2) InitSingleton(&s_singleton, &s_singletonState);
            return &s_singleton;
        }

        void* Allocate(hkUlong size, hkUint32 alignment, bool alignmentRequired);
        void  Deallocate(void* p, bool alignmentRequired);
        /// @brief The calling thread's scrap heap, created (under `name`, or a default name) on first use.
        ScrapHeap* GetThreadScrapHeap(const char* name = HK_NULL);

        /// @note Name provisional: the singleton's one-time construction.
        static void InitSingleton(MemoryManager* singleton, hkInt32* state);

        static MemoryManager s_singleton;
        static hkInt32       s_singletonState;
    };
} }

/// @brief The class allocator of Bethesda's engine classes: the engine heap (16-byte aligned for the classes that hold
///        SIMD members). A `::delete` (MSVC's deleting-destructor flag 4) still reaches the global sized operator delete.
#define CB_BETHESDA_HEAP_NEW_DELETE                                                                                       \
    HK_FORCE_INLINE void* operator new(hkUlong nbytes) { return ::CB::havok::MemoryManager::GetSingleton()->Allocate(nbytes, 0, false); } \
    HK_FORCE_INLINE void  operator delete(void* p) { ::CB::havok::MemoryManager::GetSingleton()->Deallocate(p, false); }
#define CB_BETHESDA_HEAP_ALIGNED_NEW_DELETE                                                                               \
    HK_FORCE_INLINE void* operator new(hkUlong nbytes) { return ::CB::havok::MemoryManager::GetSingleton()->Allocate(nbytes, 0x10, true); } \
    HK_FORCE_INLINE void  operator delete(void* p) { ::CB::havok::MemoryManager::GetSingleton()->Deallocate(p, true); }

namespace CB { namespace havok
{
    /// @brief A thread's scratch heap: fast stack-like allocations freed in any order.
    /// @note Opaque.
    class ScrapHeap
    {
    public:
        void* Allocate(hkUlong size, hkUlong alignment);
        void  Deallocate(void* p);
    };

    /// @brief The calling thread's memory context: the id the engine heap files allocations under.
    /// @note Name provisional; a thread-local int (the thread's TLS block +0x768, pinned in engine.yaml as
    ///       MemoryContext, kind tls). Never defined: the runtime image reaches the game's slot through its _tls_index.
    extern __declspec(thread) hkInt32 t_memoryContext;

    /// @brief Files the allocations made in its scope under `context`, restoring the thread's previous one after.
    /// @note Name provisional.
    class MemoryContextScope
    {
    public:
        HK_FORCE_INLINE explicit MemoryContextScope(hkInt32 context) : m_previous(t_memoryContext) { t_memoryContext = context; }
        HK_FORCE_INLINE ~MemoryContextScope() { t_memoryContext = m_previous; }

        hkInt32 m_previous;
    };
} }
