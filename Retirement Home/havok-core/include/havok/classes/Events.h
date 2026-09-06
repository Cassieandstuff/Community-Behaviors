#pragma once
#include "havok/classes/Base.h"

#include <cstdint>
#include <memory>
#include <string>

// Event objects + payloads, hand-ported from HKX2E Autogen. hkbEvent /
// hkbEventProperty are used inline (by value) inside other classes, so they are
// full definitions, not pointers.

namespace havok {

class hkbEventPayload;  // pointer target (defined below)

// hkbEventBase — size 16, sig 0x76bddb31. Root of the event mini-hierarchy.
class hkbEventBase : public IHavokObject {
public:
    std::int32_t                     m_id = 0;
    std::shared_ptr<hkbEventPayload> m_payload;
    HK_CLASS_ID(0x76bddb31u, "hkbEventBase")
};

// hkbEvent — size 24, sig 0x3e0fd810. Adds the (ignored) m_sender void pointer.
class hkbEvent : public hkbEventBase {
public:
    HK_CLASS_ID(0x3e0fd810u, "hkbEvent")
};

// hkbEventProperty — size 16, sig 0x0db38a15. An hkbEventBase with no extra fields.
class hkbEventProperty : public hkbEventBase {
public:
    HK_CLASS_ID(0x0db38a15u, "hkbEventProperty")
};

// hkbEventPayload — size 16, sig 0xda8c7d7d. Empty hkReferencedObject base for payloads.
class hkbEventPayload : public hkReferencedObject {
public:
    HK_CLASS_ID(0xda8c7d7du, "hkbEventPayload")
};

// hkbStringEventPayload — size 24, sig 0xed04256a.
class hkbStringEventPayload : public hkbEventPayload {
public:
    std::string m_data;
    HK_CLASS_ID(0xed04256au, "hkbStringEventPayload")
};

// hkbCondition — size 16, sig 0xda8c7d7d. Shares the signature with hkbEventPayload;
// distinguished by class name in the packfile type section, not by signature.
class hkbCondition : public hkReferencedObject {
public:
    HK_CLASS_ID(0xda8c7d7du, "hkbCondition")
};

} // namespace havok
