#pragma once
#include "havok/classes/IHavokObject.h"
#include "havok/core/HkTypes.h"

// BSPassByTargetTriggerModifier — fires an event when the character passes within
// `radius` of a moving target. hkbModifier + targetPosition(Vector4) + radius +
// movementDirection(Vector4) + triggerEvent(hkbEventProperty). Trailing targetPassed
// (bool) is SERIALIZE_IGNORED. size 160, sig 0x703d7b66. Used by dragon.

#include "havok/classes/Modifiers.h"
#include "havok/classes/Events.h"

namespace havok {

class BSPassByTargetTriggerModifier : public hkbModifier {
public:
    Vector4          m_targetPosition{};
    float            m_radius = 0.f;
    Vector4          m_movementDirection{};
    hkbEventProperty m_triggerEvent{};   // inline struct
    HK_CLASS_ID(0x703d7b66u, "BSPassByTargetTriggerModifier")
};

} // namespace havok
