#pragma once
#include "havok/classes/IHavokObject.h"
#include "havok/core/HkTypes.h"

// hkbTransformVectorModifier — applies a rotation/translation to a vector variable.
// hkbModifier + rotation(Quaternion) + translation/vectorIn/vectorOut (Vector4) +
// four bool flags. size 160, sig 0xf93e0e24. Used by dragon.

#include "havok/classes/Modifiers.h"

namespace havok {

class hkbTransformVectorModifier : public hkbModifier {
public:
    Quaternion m_rotation{};
    Vector4    m_translation{};
    Vector4    m_vectorIn{};
    Vector4    m_vectorOut{};
    bool       m_rotateOnly        = false;
    bool       m_inverse           = false;
    bool       m_computeOnActivate = false;
    bool       m_computeOnModify   = false;
    HK_CLASS_ID(0xf93e0e24u, "hkbTransformVectorModifier")
};

} // namespace havok
