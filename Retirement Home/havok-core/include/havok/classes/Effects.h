#pragma once
#include "havok/classes/Base.h"

#include <cstdint>

// Transition effects, hand-ported from HKX2E Autogen.

namespace havok {

// hkbTransitionEffect — size 80, sig 0x945da157.
class hkbTransitionEffect : public hkbGenerator {
public:
    std::int8_t m_selfTransitionMode = 0;  // enum SelfTransitionMode
    std::int8_t m_eventMode          = 0;  // enum EventMode
    std::int8_t m_defaultEventMode   = 0;  // SERIALIZE_IGNORED
    HK_CLASS_ID(0x945da157u, "hkbTransitionEffect")
};

// hkbBlendingTransitionEffect — size 144, sig 0xfd8584fe.
class hkbBlendingTransitionEffect : public hkbTransitionEffect {
public:
    float         m_duration                     = 0.f;
    float         m_toGeneratorStartTimeFraction = 0.f;
    std::uint16_t m_flags                        = 0;  // enum FlagBits
    std::int8_t   m_endMode                      = 0;  // enum EndMode
    std::int8_t   m_blendCurve                   = 0;  // enum BlendCurve
    // ── SERIALIZE_IGNORED tail (typed; kept for round-trip fidelity) ──
    float m_timeRemaining           = 0.f;
    float m_timeInTransition        = 0.f;
    bool  m_applySelfTransition     = false;
    bool  m_initializeCharacterPose = false;
    HK_CLASS_ID(0xfd8584feu, "hkbBlendingTransitionEffect")
};

} // namespace havok
