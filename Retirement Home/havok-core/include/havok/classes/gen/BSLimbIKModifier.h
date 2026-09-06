#pragma once
#include "havok/classes/IHavokObject.h"

#include <cstdint>

// BSLimbIKModifier — two-bone IK over a limb (start->end bone chain). hkbModifier +
// limitAngleDegrees + start/endBoneIndex + gain/boneRadius/castOffset. The trailing
// currentAngle/timeStep (floats) and pSkeletonMemory (pointer) are SERIALIZE_IGNORED
// runtime state. size 120, sig 0x8ea971e5. Used by dragon.

#include "havok/classes/Modifiers.h"

namespace havok {

class BSLimbIKModifier : public hkbModifier {
public:
    float        m_limitAngleDegrees = 0.f;   // +84 currentAngle (ignored)
    std::int16_t m_startBoneIndex    = 0;
    std::int16_t m_endBoneIndex      = 0;
    float        m_gain              = 0.f;
    float        m_boneRadius        = 0.f;
    float        m_castOffset        = 0.f;   // +104 timeStep (ignored) +112 pSkeletonMemory (ignored)
    HK_CLASS_ID(0x8ea971e5u, "BSLimbIKModifier")
};

} // namespace havok
