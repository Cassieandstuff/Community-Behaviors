/// @file
/// @brief NiRefObject (Gamebryo's intrusively counted base) and NiPointer, its smart pointer.
/// @note Not Havok: declared here only as far as the character controller's code uses it.
#pragma once

#include <Common/Base/hkBase.h>

extern "C" long _InterlockedIncrement(long volatile* addend);
extern "C" long _InterlockedDecrement(long volatile* addend);
#pragma intrinsic(_InterlockedIncrement, _InterlockedDecrement)

namespace CB { namespace schema
{
    /// @note 0x10 bytes.
    class NiRefObject
    {
    public:
        HK_FORCE_INLINE NiRefObject() : m_refCount(0) { _InterlockedIncrement(reinterpret_cast<volatile long*>(&ms_objects)); }
        HK_FORCE_INLINE virtual ~NiRefObject() { _InterlockedDecrement(reinterpret_cast<volatile long*>(&ms_objects)); }   ///< slot 0
        /// @brief Destroys the object once its last reference goes.
        virtual void DeleteThis();   ///< slot 1

        volatile hkUint32 m_refCount;   ///< +0x08
        hkUint32          m_pad0c;

        /// @brief How many are alive.
        static volatile hkUint32 ms_objects;

        HK_FORCE_INLINE void IncRefCount() { _InterlockedIncrement(reinterpret_cast<volatile long*>(&m_refCount)); }
        HK_FORCE_INLINE void DecRefCount()
        {
            if (_InterlockedDecrement(reinterpret_cast<volatile long*>(&m_refCount)) == 0) DeleteThis();
        }
    };

    template <class T> class NiPointer
    {
    public:
        T* m_object;

        HK_FORCE_INLINE NiPointer() : m_object(HK_NULL) {}
        /// @note Takes the reference before storing the pointer, as vanilla does.
        HK_FORCE_INLINE NiPointer(const NiPointer& other)
        {
            T* object = other.m_object;
            if (object) object->IncRefCount();
            m_object = object;
        }
        HK_FORCE_INLINE ~NiPointer()
        {
            if (m_object) m_object->DecRefCount();
        }
        /// @note The old pointer is read once, before the new one is referenced.
        HK_FORCE_INLINE NiPointer& operator=(T* object)
        {
            T* old = m_object;
            if (old != object) {
                if (object) object->IncRefCount();
                m_object = object;
                if (old) old->DecRefCount();
            }
            return *this;
        }
        HK_FORCE_INLINE T*   get() const { return m_object; }
        HK_FORCE_INLINE T*   operator->() const { return m_object; }
        HK_FORCE_INLINE      operator T*() const { return m_object; }
    };
} }
