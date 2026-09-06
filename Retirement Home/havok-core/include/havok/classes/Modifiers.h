#pragma once
#include "havok/classes/Base.h"
#include "havok/core/HkTypes.h"

#include <array>
#include <cstdint>

// Modifiers, hand-ported from HKX2E Autogen.

namespace havok {

// hkbModifier — size 80, sig 0x96ec5ced.
class hkbModifier : public hkbNode {
public:
    bool                m_enable = false;
    std::array<bool, 3> m_padModifier{};  // SERIALIZE_IGNORED (C-style array)
    HK_CLASS_ID(0x96ec5cedu, "hkbModifier")
};

// hkbTwistModifier — size 144, sig 0xb6b76b32.
class hkbTwistModifier : public hkbModifier {
public:
    Vector4      m_axisOfRotation{};
    float        m_twistAngle             = 0.f;
    std::int16_t m_startBoneIndex         = 0;
    std::int16_t m_endBoneIndex           = 0;
    std::int8_t  m_setAngleMethod         = 0;  // enum SetAngleMethod
    std::int8_t  m_rotationAxisCoordinates = 0; // enum RotationAxisCoordinates
    bool         m_isAdditive             = false;
    std::vector<std::int16_t> m_boneChainIndices;   // hkArray<hkInt16>, always empty in vanilla
    std::vector<std::int16_t> m_parentBoneIndices;  // hkArray<hkInt16>, always empty in vanilla
    HK_CLASS_ID(0xb6b76b32u, "hkbTwistModifier")
};

} // namespace havok
