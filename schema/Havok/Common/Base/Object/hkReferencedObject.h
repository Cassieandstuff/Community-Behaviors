/// @file
/// @brief hkBaseObject and hkReferencedObject: the root of every reflected Havok class.
#pragma once

#include <Common/Base/Memory/hkMemory.h>

namespace CB { namespace schema
{
    struct hkBaseObject
    {
        HK_FORCE_INLINE hkBaseObject() {}
        virtual ~hkBaseObject() {}
    };

    struct hkReferencedObject : hkBaseObject
    {
        hkUint16 m_memSizeAndFlags;   ///< +0x08
        hkInt16  m_referenceCount;    ///< +0x0a

        virtual const void* getClassType() const;                                       ///< slot 1
        virtual void        calcContentStatistics(void* collector, const void* cls) const;   ///< slot 2

        HK_FORCE_INLINE hkReferencedObject() : m_memSizeAndFlags(0xffff), m_referenceCount(1) {}
        HK_FORCE_INLINE hkReferencedObject(hkFinishLoadedObjectFlag) {}

        /// @brief The class allocator: blocks come from the thread's heap and record their size.
        /// @note `__assume` the result non-null, as vanilla does (no null check after the allocation).
        HK_FORCE_INLINE void* operator new(hkUlong nbytes)
        {
            hkReferencedObject* b = static_cast<hkReferencedObject*>(hkMemoryRouter::getInstance().heap().blockAlloc(static_cast<int>(nbytes)));
            __assume(b != 0);
            b->m_memSizeAndFlags = static_cast<hkUint16>(nbytes);
            return b;
        }
        /// @brief Class placement new.
        /// @note `__assume` the target non-null: the finish-loaded constructors construct in place with no test, while
        ///       VS2015 null-checks a placement new it cannot prove non-null (hkArray::pushBack keeps that check).
        HK_FORCE_INLINE void* operator new(hkUlong, void* p)
        {
            __assume(p != 0);
            return p;
        }
        HK_FORCE_INLINE void  operator delete(void*, void*) {}
        HK_FORCE_INLINE void  operator delete(void* p, hkUlong nbytes) { hkMemoryRouter::getInstance().heap().blockFree(p, static_cast<int>(nbytes)); }

        void addReference() const;
        void removeReference() const;
    };
} }
