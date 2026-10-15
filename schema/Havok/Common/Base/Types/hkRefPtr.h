/// @file
/// @brief hkRefPtr: the reference-counting smart pointer.
#pragma once

#include <Common/Base/Types/hkBaseTypes.h>

namespace CB { namespace schema
{
    template <class T> struct hkRefPtr
    {
        T* m_pntr;

        HK_FORCE_INLINE hkRefPtr() : m_pntr(HK_NULL) {}
        HK_FORCE_INLINE hkRefPtr(hkFinishLoadedObjectFlag) {}
        /// @note Re-reads the source after addReference, as vanilla does (the call may change it).
        HK_FORCE_INLINE hkRefPtr(const hkRefPtr& rp)
        {
            if (rp.m_pntr) rp.m_pntr->addReference();
            m_pntr = rp.m_pntr;
        }
        HK_FORCE_INLINE ~hkRefPtr()
        {
            if (m_pntr) m_pntr->removeReference();
        }

        HK_FORCE_INLINE hkRefPtr& operator=(T* e)
        {
            if (e) e->addReference();
            if (m_pntr) m_pntr->removeReference();
            m_pntr = e;
            return *this;
        }
        HK_FORCE_INLINE hkRefPtr& operator=(const hkRefPtr& rp)
        {
            if (rp.m_pntr) rp.m_pntr->addReference();
            if (m_pntr) m_pntr->removeReference();
            m_pntr = rp.m_pntr;
            return *this;
        }
        HK_FORCE_INLINE void setAndDontIncrementRefCount(T* e)
        {
            if (m_pntr && m_pntr != e) m_pntr->removeReference();
            m_pntr = e;
        }

        HK_FORCE_INLINE T* val() const { return m_pntr; }
        HK_FORCE_INLINE T* operator->() const { return m_pntr; }
        HK_FORCE_INLINE operator T*() const { return m_pntr; }
    };
} }
