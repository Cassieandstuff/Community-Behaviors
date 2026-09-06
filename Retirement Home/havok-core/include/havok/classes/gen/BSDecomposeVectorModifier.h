#pragma once
#include "havok/classes/IHavokObject.h"
#include "havok/core/HkTypes.h"

// BSDecomposeVectorModifier — splits a vector into x/y/z/w scalar outputs. hkbModifier
// + m_vector (Vector4) + x/y/z/w floats. size 112, sig 0x31f6b8b6. Used by dragon.

#include "havok/classes/Modifiers.h"

namespace havok {

class BSDecomposeVectorModifier : public hkbModifier {
public:
    Vector4 m_vector{};
    float   m_x = 0.f;
    float   m_y = 0.f;
    float   m_z = 0.f;
    float   m_w = 0.f;
    HK_CLASS_ID(0x31f6b8b6u, "BSDecomposeVectorModifier")
};

} // namespace havok
