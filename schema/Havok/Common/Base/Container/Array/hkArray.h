/// @file
/// @brief hkArray and its out-of-line growth helpers.
#pragma once

#include <Common/Base/Memory/hkMemory.h>

namespace CB { namespace schema
{
    /// @brief Whether T is plain old data: picks hkArrayUtil's construct/destruct at compile time.
    template <bool IS_POD> struct hkArrayIsPod {};
    /// @brief Whether containers may treat T as plain old data: the compiler's __is_pod, or a type that says so with
    ///        HK_DECLARE_POD_TYPE() (Havok's marker for structs whose members, such as hkBool, have constructors).
    /// @note Not an enum: Havok's trait lists the POD types explicitly and enums are not among them, so vanilla fills an
    ///       hkArray of an enum through the null-checked placement new (hkaDetectRagdollPenetration's bone states).
    template <class T> struct hkIsPodType
    {
        template <class U> static char test(typename U::hkIsPodTypeTag*);
        template <class U> static int  test(...);
        static const bool result = (__is_pod(T) && !__is_enum(T)) || sizeof(test<T>(0)) == 1;
    };
    /// @note Havok declares hkBool POD (it has a constructor, so __is_pod says no): vanilla fills and copies an
    ///       hkArray<hkBool> as raw bytes.
    template <> struct hkIsPodType<hkBool> { static const bool result = true; };
    struct hkQsTransform;
    /// @note Havok declares its math types POD: arrays of them copy element-wise (three movaps), no construct loop.
    template <> struct hkIsPodType<hkQsTransform> { enum { result = 1 }; };

    struct hkArrayUtil
    {
        static void _reserve(hkMemoryAllocator& a, void* array, int reqCapacity, int sizeElem);
        /// @brief Grows a full array (pushBack's slow path).
        static void _reserveMore(hkMemoryAllocator& a, void* array, int sizeElem);

        template <class T> static HK_FORCE_INLINE void construct(T*, int, hkArrayIsPod<true>) {}
        /// @note Through the global placement new: vanilla null-checks each slot.
        template <class T> static HK_FORCE_INLINE void construct(T* base, int numElements, hkArrayIsPod<false>)
        {
            for (int i = 0; i < numElements; i++) new (base + i) T();
        }
        /// @brief Assigns `numElements` elements from `src`.
        /// @note The pointers are parameters: vanilla reads both arrays' data once, not again after each element's calls.
        template <class T> static HK_FORCE_INLINE void copy(T* dst, const T* src, int numElements)
        {
            for (int i = 0; i < numElements; i++) dst[i] = src[i];
        }
        /// @brief Assigns `numElements` elements from `src` into raw storage.
        template <class T> static HK_FORCE_INLINE void constructWithArray(T* base, int numElements, const T* src, hkArrayIsPod<true>)
        {
            for (int i = 0; i < numElements; i++) base[i] = src[i];
        }
        /// @brief Copy-constructs `numElements` elements from `src` (through the global placement new, null-checked).
        template <class T> static HK_FORCE_INLINE void constructWithArray(T* base, int numElements, const T* src, hkArrayIsPod<false>)
        {
            for (int i = 0; i < numElements; i++) new (base + i) T(src[i]);
        }
        /// @brief Constructs `numElements` copies of `t`.
        template <class T> static HK_FORCE_INLINE void constructWithCopy(T* base, int numElements, const T& t, hkArrayIsPod<true>)
        {
            for (int i = 0; i < numElements; i++) base[i] = t;
        }
        /// @note Through the global placement new: vanilla null-checks each slot.
        template <class T> static HK_FORCE_INLINE void constructWithCopy(T* base, int numElements, const T& t, hkArrayIsPod<false>)
        {
            for (int i = 0; i < numElements; i++) new (base + i) T(t);
        }
        template <class T> static HK_FORCE_INLINE void destruct(T*, int, hkArrayIsPod<true>) {}
        /// @note Last to first, as vanilla walks them.
        template <class T> static HK_FORCE_INLINE void destruct(T* base, int numElements, hkArrayIsPod<false>)
        {
            for (int i = numElements - 1; i >= 0; i--) base[i].~T();
        }
    };

    template <class T> struct hkArray
    {
        enum { CAPACITY_MASK = 0x3fffffff, DONT_DEALLOCATE_FLAG = int(0x80000000) };

        T*  m_data;
        int m_size;
        int m_capacityAndFlags;

        /// @brief The class allocator (an array held by pointer, e.g. hkbBehaviorGraph::m_activeNodes).
        /// @note `throw()`: vanilla null-checks the block before constructing in it.
        HK_FORCE_INLINE void* operator new(hkUlong nbytes) throw() { return hkMemoryRouter::getInstance().heap().blockAlloc(static_cast<int>(nbytes)); }
        HK_FORCE_INLINE void  operator delete(void* p, hkUlong nbytes) { hkMemoryRouter::getInstance().heap().blockFree(p, static_cast<int>(nbytes)); }
        /// @note hkArrayUtil::constructWithArray uses the global placement new; this keeps it reachable.
        HK_FORCE_INLINE void* operator new(hkUlong, void* p) throw() { return p; }
        HK_FORCE_INLINE void  operator delete(void*, void*) {}

        HK_FORCE_INLINE hkArray() : m_data(HK_NULL), m_size(0), m_capacityAndFlags(DONT_DEALLOCATE_FLAG) {}
        /// @brief Over caller-owned storage (`capacity` elements at `buffer`, `size` in use), never freed.
        HK_FORCE_INLINE hkArray(T* buffer, int size, int capacity)
        :   m_data(buffer), m_size(size), m_capacityAndFlags(capacity | DONT_DEALLOCATE_FLAG) {}
        /// @note Leaves the loaded array as it is.
        HK_FORCE_INLINE hkArray(hkFinishLoadedObjectFlag) {}
        /// @brief Destroys the elements (last to first), frees owned storage and leaves the array empty and non-owning.
        HK_FORCE_INLINE ~hkArray()
        {
            hkArrayUtil::destruct(m_data, m_size, hkArrayIsPod<hkIsPodType<T>::result != 0>());
            m_size = 0;
            if (m_capacityAndFlags >= 0) {
                hkMemoryAllocator& a = hkContainerHeapAllocator::s_alloc;
                a.bufFree(m_data, getCapacity() * int(sizeof(T)));
            }
            m_data             = HK_NULL;
            m_capacityAndFlags = DONT_DEALLOCATE_FLAG;
        }

        HK_FORCE_INLINE hkArray& operator=(const hkArray& a) { return assign(a, hkArrayIsPod<hkIsPodType<T>::result != 0>()); }

        /// @brief Plain-old-data elements: reallocates (to exactly `a`'s size) only when the capacity is short, then copies.
        /// @note Vanilla's copy loop counts up (`inc; cmp; jl`); VS2015 24247 counts this spelling (and every other tried)
        ///       down, which keeps the clip's getInternalState/setInternalState STRUCTURAL.
        HK_FORCE_INLINE hkArray& assign(const hkArray& a, hkArrayIsPod<true>)
        {
            if (getCapacity() < a.getSize()) {
                hkMemoryAllocator& alloc = hkContainerHeapAllocator::s_alloc;
                if (m_capacityAndFlags >= 0) alloc.bufFree(m_data, getCapacity() * int(sizeof(T)));
                int numBytes       = a.getSize() * int(sizeof(T));
                m_data             = static_cast<T*>(alloc.bufAlloc(numBytes));
                m_capacityAndFlags = numBytes / int(sizeof(T));
            }
            const int n = a.getSize();
            m_size      = n;
            T*       d  = m_data;
            const T* s  = a.m_data;
            for (int i = 0; i < n; i++) d[i] = s[i];
            return *this;
        }

        /// @brief Destroys the elements (last to first; a no-op for POD) and empties the array, keeping its storage.
        HK_FORCE_INLINE void clear()
        {
            hkArrayUtil::destruct(m_data, m_size, hkArrayIsPod<hkIsPodType<T>::result != 0>());
            m_size = 0;
        }
        /// @brief Empties the array and frees owned storage, leaving it empty and non-owning (the destructor's body).
        HK_FORCE_INLINE void clearAndDeallocate()
        {
            m_size = 0;
            if (m_capacityAndFlags >= 0) {
                hkMemoryAllocator& a = hkContainerHeapAllocator::s_alloc;
                a.bufFree(m_data, getCapacity() * int(sizeof(T)));
            }
            m_data             = HK_NULL;
            m_capacityAndFlags = DONT_DEALLOCATE_FLAG;
        }
        HK_FORCE_INLINE int      getSize() const { return m_size; }
        HK_FORCE_INLINE int      getCapacity() const { return m_capacityAndFlags & CAPACITY_MASK; }
        HK_FORCE_INLINE T&       operator[](int i) { return m_data[i]; }
        HK_FORCE_INLINE const T& operator[](int i) const { return m_data[i]; }
        /// @brief The last index holding `t`, -1 if none (searched from the end).
        HK_FORCE_INLINE int lastIndexOf(const T& t) const
        {
            for (int i = m_size - 1; i >= 0; i--) {
                if (m_data[i] == t) return i;
            }
            return -1;
        }

        /// @brief Destroys the elements past `n` and default-constructs the new ones (both no-ops for POD).
        HK_FORCE_INLINE void setSize(int n)
        {
            reserve(n);
            hkArrayUtil::destruct(m_data + n, m_size - n, hkArrayIsPod<hkIsPodType<T>::result != 0>());
            hkArrayUtil::construct(m_data + m_size, n - m_size, hkArrayIsPod<hkIsPodType<T>::result != 0>());
            m_size = n;
        }
        /// @brief As setSize(n), with the new elements copies of `fill`.
        HK_FORCE_INLINE void setSize(int n, const T& fill)
        {
            reserve(n);
            hkArrayUtil::destruct(m_data + n, m_size - n, hkArrayIsPod<hkIsPodType<T>::result != 0>());
            hkArrayUtil::constructWithCopy(m_data + m_size, n - m_size, fill, hkArrayIsPod<hkIsPodType<T>::result != 0>());
            m_size = n;
        }
        /// @brief Other elements: assigns over the ones both arrays have, destroys this one's surplus and copy-constructs
        ///        the rest.
        HK_FORCE_INLINE hkArray& assign(const hkArray& a, hkArrayIsPod<false>)
        {
            const int oldSize    = m_size;
            const int newSize    = a.getSize();
            const int copiedSize = (newSize > oldSize) ? oldSize : newSize;
            reserve(newSize);
            hkArrayUtil::destruct(m_data + newSize, oldSize - newSize, hkArrayIsPod<false>());
            hkArrayUtil::copy(m_data, a.m_data, copiedSize);
            hkArrayUtil::constructWithArray(m_data + copiedSize, newSize - copiedSize, a.m_data + copiedSize, hkArrayIsPod<false>());
            m_size = newSize;
            return *this;
        }
        /// @brief Removes element `index`, moving the last element into its place.
        HK_FORCE_INLINE void removeAt(int index)
        {
            hkArrayUtil::destruct(m_data + index, 1, hkArrayIsPod<__is_pod(T)>());
            m_size--;
            if (m_size != index) hkMemUtil::memCpyOne<sizeof(T)>(m_data + index, m_data + m_size);
        }
        HK_FORCE_INLINE void removeAtAndCopy(int index)
        {
            m_size--;
            hkMemUtil::memCpy8(m_data + index, m_data + index + 1, (m_size - index) * int(sizeof(T)));
        }
        HK_FORCE_INLINE void reserve(int n)
        {
            if (getCapacity() < n) {
                const int cap2    = 2 * getCapacity();
                const int newSize = (n < cap2) ? cap2 : n;
                hkArrayUtil::_reserve(hkContainerHeapAllocator::s_alloc, this, newSize, sizeof(T));
            }
        }
        /// @brief Grows to exactly `n` when the capacity is short (no doubling).
        HK_FORCE_INLINE void reserveExactly(int n)
        {
            if (getCapacity() < n) hkArrayUtil::_reserve(hkContainerHeapAllocator::s_alloc, this, n, sizeof(T));
        }
        HK_FORCE_INLINE T*       begin() { return m_data; }
        HK_FORCE_INLINE const T* begin() const { return m_data; }
        /// @note A non-POD element constructs through the global placement new (vanilla null-checks the slot); a POD one
        ///       is assigned, unchecked.
        HK_FORCE_INLINE void pushBack(const T& t)
        {
            if (m_size == getCapacity()) hkArrayUtil::_reserveMore(hkContainerHeapAllocator::s_alloc, this, sizeof(T));
            hkArrayUtil::constructWithCopy(m_data + m_size, 1, t, hkArrayIsPod<hkIsPodType<T>::result != 0>());
            m_size++;
        }
        /// @brief Appends `t` without a capacity check.
        /// @pre The array has room for it.
        HK_FORCE_INLINE void pushBackUnchecked(const T& t)
        {
            m_data[m_size] = t;
            m_size++;
        }
        /// @brief Appends one element left unconstructed and returns it.
        HK_FORCE_INLINE T& expandOne()
        {
            if (m_size == getCapacity()) hkArrayUtil::_reserveMore(hkContainerHeapAllocator::s_alloc, this, sizeof(T));
            return m_data[m_size++];
        }
        /// @brief Appends `numElemsToAppend` elements copied from `a`, growing (at least doubling) when short.
        HK_FORCE_INLINE void append(const T* a, int numElemsToAppend) { _append(hkContainerHeapAllocator::s_alloc, a, numElemsToAppend); }
        /// @note Out of line in vanilla (Havok's hkArrayBase::_append, not inline): an instance per T, compiled in the
        ///       caller's unit (hkbBlendingTransitionEffect's for hkQsTransform).
        void _append(hkMemoryAllocator& alloc, const T* a, int numElemsToAppend);
    };

    template <class T>
    void hkArray<T>::_append(hkMemoryAllocator& alloc, const T* a, int numElemsToAppend)
    {
        const int newSize = m_size + numElemsToAppend;
        if (newSize > getCapacity()) {
            if (getCapacity() < newSize) {
                const int cap2 = 2 * getCapacity();
                hkArrayUtil::_reserve(alloc, this, (newSize < cap2) ? cap2 : newSize, sizeof(T));
            }
        }
        hkArrayUtil::constructWithArray(m_data + m_size, numElemsToAppend, a, hkArrayIsPod<hkIsPodType<T>::result != 0>());
        m_size = newSize;
    }
    /// @brief Havok's hkArray<T, hkContainerTempAllocator>: `n` elements whose storage comes from the temp allocator.
    /// @note A subclass rather than hkArray's allocator argument, so hkArray<T>'s mangled names (pinned in engine.yaml)
    ///       stay as they are. Its destructor hands the block back to the temp allocator and leaves the base nothing to
    ///       free.
    template <class T> struct hkTempArray : hkArray<T>
    {
        HK_FORCE_INLINE explicit hkTempArray(int n)
        {
            int capacity = 0;
            T*  data     = HK_NULL;
            if (n) {
                int numBytes = n * int(sizeof(T));
                hkMemoryAllocator& a = hkContainerTempAllocator::s_alloc;
                data                 = static_cast<T*>(a.bufAlloc(numBytes));
                capacity     = numBytes / int(sizeof(T));
            }
            this->m_data             = data;
            this->m_size             = n;
            this->m_capacityAndFlags = capacity ? capacity : int(hkArray<T>::DONT_DEALLOCATE_FLAG);
        }
        HK_FORCE_INLINE ~hkTempArray()
        {
            this->m_size = 0;
            if (this->m_capacityAndFlags >= 0) {
                hkMemoryAllocator& a = hkContainerTempAllocator::s_alloc;
                a.bufFree(this->m_data, this->getCapacity() * int(sizeof(T)));
            }
            this->m_data             = HK_NULL;
            this->m_capacityAndFlags = hkArray<T>::DONT_DEALLOCATE_FLAG;
        }
    };

    /// @brief An array with room for N elements in place (no allocation until it outgrows them).
    template <class T, unsigned N> struct hkInplaceArray : hkArray<T>
    {
        T m_storage[N];

        HK_FORCE_INLINE hkInplaceArray()
        {
            this->m_data             = m_storage;
            this->m_size             = 0;
            this->m_capacityAndFlags = int(N) | hkArray<T>::DONT_DEALLOCATE_FLAG;
        }
    };
} }
