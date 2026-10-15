/// @file
/// @brief Behavior events: the id + payload base, the sent event (with sender), the stored property, and event info.
#pragma once

#include <Common/Base/hkBase.h>

namespace CB { namespace schema
{
    class hkbNode;

    struct hkbEventPayload : hkReferencedObject {};

    struct hkbEventBase
    {
        int              m_id;
        hkbEventPayload* m_payload;

        HK_FORCE_INLINE int              getId() const { return m_id; }
        HK_FORCE_INLINE hkbEventPayload* getPayload() const { return m_payload; }
    };

    /// @brief An event in flight: what hkbEventQueue carries.
    struct hkbEvent : hkbEventBase
    {
        hkbNode* m_sender;

        HK_FORCE_INLINE hkbEvent(int id, hkbEventPayload* payload, hkbNode* sender)
        {
            m_id      = id;
            m_payload = payload;
            m_sender  = sender;
        }
    };

    /// @brief An event as a node stores it: id + ref-counted payload, no sender.
    /// @note Copies default the payload to null and then assign it — vanilla's store-null / addRef / removeRef / store.
    ///       The source payload is read once, before the addReference: vanilla holds it across the call (a
    ///       hkRefPtr-to-hkRefPtr assign would re-read it).
    struct hkbEventProperty
    {
        int                       m_id;
        hkRefPtr<hkbEventPayload> m_payload;

        HK_FORCE_INLINE hkbEventProperty() : m_id(-1) {}
        HK_FORCE_INLINE hkbEventProperty(const hkbEventBase& e) : m_id(e.m_id) { m_payload = e.m_payload; }
        HK_FORCE_INLINE hkbEventProperty(const hkbEventProperty& e) : m_id(e.m_id) { m_payload = e.getPayload(); }
        HK_FORCE_INLINE hkbEventProperty& operator=(const hkbEventProperty& e)
        {
            m_id      = e.m_id;
            m_payload = e.getPayload();
            return *this;
        }

        HK_FORCE_INLINE int              getId() const { return m_id; }
        HK_FORCE_INLINE hkbEventPayload* getPayload() const { return m_payload; }
    };

    struct hkbEventInfo
    {
        enum { FLAG_SILENT = 0x1 };
        hkUint32 m_flags;
    };
} }
