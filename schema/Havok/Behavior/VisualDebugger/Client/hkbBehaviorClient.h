/// @file
/// @brief hkbBehaviorClient: the behavior visual-debugger client (what the tool side of a remote session keeps).
#pragma once

#include <Behavior/VisualDebugger/Client/hkbClientCharacterState.h>
#include <Common/Base/Container/StringMap/hkStringMap.h>

namespace CB { namespace schema
{
    struct hkClass;

    /// @brief Receives the objects a remote session sends.
    /// @note 0x10 bytes. Vanilla's vtable (0x1419fc8f8) has the destructor and one pure slot.
    class hkRemoteObjectClientSideListener
    {
    public:
        hkUint8 m_pad08[0x10 - 0x08];

        /// @brief The class allocator's delete: the block goes back to the thread's heap with the static size.
        HK_FORCE_INLINE void operator delete(void* p, hkUlong nbytes) { hkMemoryRouter::getInstance().heap().blockFree(p, static_cast<int>(nbytes)); }

        /// @note User-provided: vanilla's destructor chain stores this vtable.
        virtual ~hkRemoteObjectClientSideListener() {}
        /// @note Name per the Havok SDK, unverified here.
        virtual void receiveObjectCallback(hkReferencedObject* object, const hkClass* klass) = 0;   ///< slot 1
    };

    /// @note 0x80 bytes; not reflected. Members are named only where the destructor shows their type; names provisional.
    class hkbBehaviorClient : public hkReferencedObject, public hkRemoteObjectClientSideListener
    {
    public:
        /// @note The deleting destructor goes through hkReferencedObject's allocator (the recorded size).
        using hkReferencedObject::operator delete;

        hkArray< hkRefPtr<hkReferencedObject> > m_objects;      ///< +0x20 — referenced
        hkArray<hkUlong>                        m_ids;          ///< +0x30
        hkRefPtr<hkReferencedObject>            m_object;       ///< +0x40
        hkStringMap<int>                        m_nameMapA;     ///< +0x48
        hkStringMap<int>                        m_nameMapB;     ///< +0x60
        hkUint8                                 m_pad78[0x80 - 0x78];

        /// @note User-provided: vanilla's out-of-line copy (0x140f3e910) stores both of the class's vtables first.
        ~hkbBehaviorClient();

        virtual void receiveObjectCallback(hkReferencedObject* object, const hkClass* klass);   ///< (listener) slot 1; out of line in vanilla (0x140f3f710)
    };
} }
