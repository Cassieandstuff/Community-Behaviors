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
        /// @brief Grows the array by one element (through `allocator` when `capacity` is used up), uninitialized.
        /// @return The new element's index, 0xffffffff if the allocation failed.
        /// @note Name provisional; out of line in vanilla (0x140cc6890).
        hkUint32 PrepareForPush(IAllocatorFunctor& allocator, hkUint32 capacity, hkUint32 elemSize);
        /// @brief Moves `count` elements of `elemSize` bytes in `data` from `srcIndex` to `dstIndex` (overlap allowed).
        /// @note Name provisional; out of line in vanilla (0x140cc67f0).
        void MoveItems(void* data, hkUint32 dstIndex, hkUint32 srcIndex, hkUint32 count, hkUint32 elemSize);

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
        /// @brief Appends `value`, dropping it if the array cannot grow.
        HK_FORCE_INLINE void PushBack(const T& value)
        {
            hkUint32 index;
            {
                BSTArrayAllocatorFunctor<BSScrapArrayAllocator> allocator(this);
                index = PrepareForPush(allocator, m_capacity, sizeof(T));
            }
            if (index != 0xffffffffu) static_cast<T*>(m_data)[index] = value;
        }
        /// @brief Destroys every element (keeping the buffer).
        HK_FORCE_INLINE void Clear()
        {
            T* element = static_cast<T*>(m_data);
            if (element) {
                for (hkUint32 n = m_size; n; --n, ++element) element->~T();
                m_size = 0;
            }
        }
        /// @brief Grows (value-initializing the new elements) or shrinks (destroying the dropped ones) to `size`.
        /// @return False if the buffer could not grow (the array is left as it was).
        /// @note Out of line in vanilla (a COMDAT per element type).
        bool Resize(hkUint32 size);
    };

    /// @note 0x18 bytes.
    template <class T> class BSTArray : public BSTArrayHeapAllocator, public BSTArrayBase
    {
    };

    /// @brief The allocator half of a BSTSmallArray: `N` bytes in place, the heap past them.
    /// @note 8 + N bytes.
    template <hkUint32 N> class BSTSmallArrayHeapAllocator
    {
    public:
        HK_FORCE_INLINE void* GetData() { return m_local ? static_cast<void*>(m_data.m_local) : m_data.m_heap; }

        hkUint32 m_capacity : 31;   ///< +0x00
        /// @brief Whether the elements are in m_data.m_local.
        hkUint32 m_local : 1;
        hkUint32 m_pad04;
        union
        {
            void* m_heap;
            char  m_local[N];
        } m_data;                   ///< +0x08
    };

    /// @brief A BSTArray with room for `N` elements in place.
    template <class T, hkUint32 N> class BSTSmallArray : public BSTSmallArrayHeapAllocator<sizeof(T) * N>, public BSTArrayBase
    {
    public:
        typedef BSTSmallArrayHeapAllocator<sizeof(T) * N> Allocator;

        HK_FORCE_INLINE hkUint32 GetSize() const { return m_size; }
        HK_FORCE_INLINE T&       operator[](hkUint32 i) { return static_cast<T*>(this->GetData())[i]; }
        /// @brief Appends `value`.
        /// @warning No check that the array could grow (as vanilla's).
        HK_FORCE_INLINE void PushBack(const T& value)
        {
            BSTArrayAllocatorFunctor<Allocator> allocator(this);
            const hkUint32 index = PrepareForPush(allocator, this->m_capacity, sizeof(T));
            static_cast<T*>(this->GetData())[index] = value;
        }
        /// @brief Removes the first element equal to `value`, moving the last element into its place.
        /// @return Whether there was one.
        HK_FORCE_INLINE bool RemoveFast(const T& value)
        {
            hkUint32 index = 0xffffffffu;
            for (hkUint32 i = 0; i < m_size && index == 0xffffffffu; ++i) {
                if ((*this)[i] == value) index = i;
            }
            if (index != 0xffffffffu) {
                const hkUint32 after = m_size - index - 1;
                const hkUint32 moved = after < 1 ? after : 1;
                MoveItems(this->GetData(), index, m_size - moved, moved, sizeof(T));
                --m_size;
            }
            return index != 0xffffffffu;
        }
    };
} }
