#pragma once
#include "havok/classes/Base.h"
#include "havok/classes/Events.h"

#include <cstdint>
#include <memory>
#include <vector>

// Array-wrapper + element-struct classes, hand-ported from HKX2E Autogen.
// `structarray` elements are stored inline by value (std::vector<T>); pointer
// arrays elsewhere use std::vector<std::shared_ptr<T>>.

namespace havok {

class hkbTransitionEffect;  // pointer target (defined in Effects.h)

// hkbStateMachineTimeInterval — size 16, sig 0x60a881e5.
class hkbStateMachineTimeInterval : public IHavokObject {
public:
    std::int32_t m_enterEventId = 0;
    std::int32_t m_exitEventId  = 0;
    float        m_enterTime    = 0.f;
    float        m_exitTime     = 0.f;
    HK_CLASS_ID(0x60a881e5u, "hkbStateMachineTimeInterval")
};

// hkbStateMachineTransitionInfo — size 72, sig 0xcdec8025.
class hkbStateMachineTransitionInfo : public IHavokObject {
public:
    hkbStateMachineTimeInterval          m_triggerInterval{};   // inline
    hkbStateMachineTimeInterval          m_initiateInterval{};  // inline
    std::shared_ptr<hkbTransitionEffect> m_transition;
    std::shared_ptr<hkbCondition>        m_condition;
    std::int32_t m_eventId           = 0;
    std::int32_t m_toStateId         = 0;
    std::int32_t m_fromNestedStateId = 0;
    std::int32_t m_toNestedStateId   = 0;
    std::int16_t m_priority          = 0;
    std::int16_t m_flags             = 0;  // enum TransitionFlags
    HK_CLASS_ID(0xcdec8025u, "hkbStateMachineTransitionInfo")
};

// hkbStateMachineTransitionInfoArray — size 32, sig 0xe397b11e.
class hkbStateMachineTransitionInfoArray : public hkReferencedObject {
public:
    std::vector<hkbStateMachineTransitionInfo> m_transitions;
    HK_CLASS_ID(0xe397b11eu, "hkbStateMachineTransitionInfoArray")
};

// hkbStateMachineEventPropertyArray — size 32, sig 0xb07b4388.
class hkbStateMachineEventPropertyArray : public hkReferencedObject {
public:
    std::vector<hkbEventProperty> m_events;
    HK_CLASS_ID(0xb07b4388u, "hkbStateMachineEventPropertyArray")
};

// hkbClipTrigger — size 32, sig 0x7eb45cea.
class hkbClipTrigger : public IHavokObject {
public:
    float            m_localTime = 0.f;
    hkbEventProperty m_event{};   // inline
    bool m_relativeToEndOfClip = false;
    bool m_acyclic             = false;
    bool m_isAnnotation        = false;
    HK_CLASS_ID(0x7eb45ceau, "hkbClipTrigger")
};

// hkbClipTriggerArray — size 32, sig 0x59c23a0f.
class hkbClipTriggerArray : public hkReferencedObject {
public:
    std::vector<hkbClipTrigger> m_triggers;
    HK_CLASS_ID(0x59c23a0fu, "hkbClipTriggerArray")
};

// hkbBoneWeightArray — size 64, sig 0xcd902b77.
class hkbBoneWeightArray : public hkbBindable {
public:
    std::vector<float> m_boneWeights;
    HK_CLASS_ID(0xcd902b77u, "hkbBoneWeightArray")
};

} // namespace havok
