/// @file
/// @brief BSTArray: Bethesda's growable array (a heap allocator base and a size base).
/// @note Not Havok: declared only as far as the character controller's code constructs one.
#pragma once

#include <Common/Base/hkBase.h>

namespace CB { namespace schema
{
    /// @note 0x10 bytes: the buffer and its capacity.
    class BSTArrayHeapAllocator
    {
    public:
        /// @note Out of line in vanilla.
        BSTArrayHeapAllocator();
        /// @note Out of line in vanilla.
        ~BSTArrayHeapAllocator();

        void*    m_data;       ///< +0x00
        hkUint32 m_capacity;   ///< +0x08
        hkUint32 m_pad0c;
    };

    /// @note 0x8 bytes: the element count.
    class BSTArrayBase
    {
    public:
        /// @brief How an array grows: the allocator half of a BSTArray, behind an interface.
        class IAllocatorFunctor
        {
        public:
            virtual bool Allocate(hkUint32 num, hkUint32 elemSize) = 0;
            virtual bool Reallocate(hkUint32 minNewSize, hkUint32 frontCopyCount, hkUint32 shiftCount, hkUint32 backCopyCount, hkUint32 elemSize) = 0;
            virtual void Deallocate() = 0;
            virtual ~IAllocatorFunctor() {}
        };

        /// @note Out of line in vanilla.
        BSTArrayBase();
        /// @note Out of line in vanilla (empty).
        ~BSTArrayBase();

        /// @brief Allocates room for `capacity` elements of `elemSize` bytes (nothing for 0).
        /// @note Out of line in vanilla.
        bool InitialReserve(IAllocatorFunctor& allocator, hkUint32 capacity, hkUint32 elemSize);

        hkUint32 m_size;   ///< +0x00
        hkUint32 m_pad04;
    };

    /// @brief Adapts an allocator class to BSTArrayBase::IAllocatorFunctor.
    template <class Allocator> class BSTArrayAllocatorFunctor : public BSTArrayBase::IAllocatorFunctor
    {
    public:
        HK_FORCE_INLINE explicit BSTArrayAllocatorFunctor(Allocator* allocator) : m_allocator(allocator) {}

        virtual bool Allocate(hkUint32 num, hkUint32 elemSize);
        virtual bool Reallocate(hkUint32 minNewSize, hkUint32 frontCopyCount, hkUint32 shiftCount, hkUint32 backCopyCount, hkUint32 elemSize);
        virtual void Deallocate();

        Allocator* m_allocator;
    };

    /// @brief The allocator half of a BSScrapArray: the calling thread's scrap heap.
    /// @note 0x18 bytes.
    class BSScrapArrayAllocator
    {
    public:
        HK_FORCE_INLINE BSScrapArrayAllocator() : m_heap(HK_NULL), m_data(HK_NULL), m_capacity(0) {}

        /// @brief Frees the buffer (and forgets the heap and the capacity).
        /// @note Out of line in vanilla.
        void Deallocate();

        void*    m_heap;       ///< +0x00 — ScrapHeap*
        void*    m_data;       ///< +0x08
        hkUint32 m_capacity;   ///< +0x10
        hkUint32 m_pad14;
    };

    /// @brief A BSTArray on the thread's scrap heap, for a function's scratch lists.
    /// @note 0x20 bytes.
    template <class T> class BSScrapArray : public BSScrapArrayAllocator, public BSTArrayBase
    {
    public:
        HK_FORCE_INLINE explicit BSScrapArray(hkUint32 capacity)
        {
            BSTArrayAllocatorFunctor<BSScrapArrayAllocator> allocator(this);
            InitialReserve(allocator, capacity, sizeof(T));
        }
        HK_FORCE_INLINE ~BSScrapArray()
        {
            if (m_data) {
                BSScrapArrayAllocator::Deallocate();
                m_size = 0;
            }
        }

        HK_FORCE_INLINE hkUint32 GetSize() const { return m_size; }
        HK_FORCE_INLINE T&       operator[](hkUint32 i) { return static_cast<T*>(m_data)[i]; }
    };

    /// @note 0x18 bytes.
    template <class T> class BSTArray : public BSTArrayHeapAllocator, public BSTArrayBase
    {
    };
} }
