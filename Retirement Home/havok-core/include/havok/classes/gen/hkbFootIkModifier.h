#pragma once
#include "havok/classes/IHavokObject.h"
#include "havok/core/HkTypes.h"

#include <cstdint>
#include <vector>

// hkbFootIkModifier — full foot IK (raycast to ground, per-leg ankle placement).
// hkbModifier + gains(hkbFootIkGains) + legs(array) + raycast/error/align params.
// The trailing internalLegData/prevIsFootIkEnabled/isSetUp/isGroundPositionValid/
// timeStep members are SERIALIZE_IGNORED runtime state. size 256, sig 0xed8966c0.
// Used by dragon. (Distinct from hkbFootIkControlsModifier — different field set.)

#include "havok/classes/Modifiers.h"
#include "havok/classes/gen/hkbFootIkGains.h"
#include "havok/classes/gen/hkbFootIkModifierLeg.h"

namespace havok {

class hkbFootIkModifier : public hkbModifier {
public:
    hkbFootIkGains                    m_gains{};        // +80 inline struct (48 bytes)
    std::vector<hkbFootIkModifierLeg> m_legs;           // +128
    float      m_raycastDistanceUp     = 0.f;           // +144
    float      m_raycastDistanceDown   = 0.f;           // +148
    float      m_originalGroundHeightMS = 0.f;          // +152
    float      m_errorOut              = 0.f;           // +156
    Vector4    m_errorOutTranslation{};                 // +160
    Quaternion m_alignWithGroundRotation{};             // +176
    float      m_verticalOffset        = 0.f;           // +192
    std::uint32_t m_collisionFilterInfo = 0;            // +196
    float      m_forwardAlignFraction  = 0.f;           // +200
    float      m_sidewaysAlignFraction = 0.f;           // +204
    float      m_sidewaysSampleWidth   = 0.f;           // +208
    bool       m_useTrackData          = false;         // +212
    bool       m_lockFeetWhenPlanted   = false;         // +213
    bool       m_useCharacterUpVector  = false;         // +214
    std::int8_t m_alignMode            = 0;             // +215 (ignored tail to +256)
    HK_CLASS_ID(0xed8966c0u, "hkbFootIkModifier")
};

} // namespace havok
