#pragma once
#include "havok/classes/IHavokObject.h"
#include "havok/core/HkTypes.h"
#include "havok/classes/Events.h"

#include <cstdint>

// One entry of hkbFootIkModifier::m_legs. Inline array value type (no packfile
// __classnames__ entry) — a plain struct with Read/Write, not a registered
// IHavokObject. size 160. The leading originalAnkleTransformMS (hkQsTransform, 48
// bytes) is runtime state; we skip its 48 bytes rather than model the transform.

namespace havok {

struct hkbFootIkModifierLeg {
    // +0  originalAnkleTransformMS (hkQsTransform, 48 bytes) — runtime state, skipped.
    Quaternion       m_prevAnkleRotLS{};              // +48
    Vector4          m_kneeAxisLS{};                  // +64
    Vector4          m_footEndLS{};                   // +80
    hkbEventProperty m_ungroundedEvent{};             // +96 (payload ptr at +104)
    float            m_footPlantedAnkleHeightMS = 0.f;// +112
    float            m_footRaisedAnkleHeightMS  = 0.f;// +116
    float            m_maxAnkleHeightMS         = 0.f;// +120
    float            m_minAnkleHeightMS         = 0.f;// +124
    float            m_maxKneeAngleDegrees      = 0.f;// +128
    float            m_minKneeAngleDegrees      = 0.f;// +132
    float            m_verticalError            = 0.f;// +136
    float            m_maxAnkleAngleDegrees     = 0.f;// +140
    std::int16_t     m_hipIndex   = 0;                // +144
    std::int16_t     m_kneeIndex  = 0;                // +146
    std::int16_t     m_ankleIndex = 0;                // +148
    bool             m_hitSomething = false;          // +150
    bool             m_isPlantedMS  = false;          // +151
    bool             m_isOriginalAnkleTransformMSSet = false; // +152 (pad to 160)

    void Read(PackFileDeserializer& des, BinaryReaderEx& br);
    void Write(PackFileSerializer& s, BinaryWriterEx& bw) const;
};

} // namespace havok
