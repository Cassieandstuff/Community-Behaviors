#pragma once
#include "havok/classes/IHavokObject.h"
#include "havok/core/HkTypes.h"

// BSTweenerModifier — tweens the character toward a target position/rotation over a
// duration. hkbModifier + three bools + tweenDuration + targetPosition(Vector4) +
// targetRotation(Quaternion). Trailing duration/startTransform/time are
// SERIALIZE_IGNORED runtime state. size 208, sig 0x0d2d9a04. Used by dragon.

#include "havok/classes/Modifiers.h"

namespace havok {

class BSTweenerModifier : public hkbModifier {
public:
    bool       m_tweenPosition    = false;
    bool       m_tweenRotation    = false;
    bool       m_useTweenDuration = false;
    float      m_tweenDuration    = 0.f;
    Vector4    m_targetPosition{};
    Quaternion m_targetRotation{};
    HK_CLASS_ID(0x0d2d9a04u, "BSTweenerModifier")
};

} // namespace havok
