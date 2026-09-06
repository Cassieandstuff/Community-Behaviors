#pragma once
#include "havok/classes/IHavokObject.h"

// BSIStateManagerModifier — Bethesda modifier that drives an iState variable from a set
// of (state machine, state id) tuples. hkbModifier + m_iStateVar (int) + m_stateData
// (array of BSIStateManagerModifierBSiStateData). size 104, sig 0x6cb24f2e. Used by
// spriggan, vampire lord, werewolf.

#include "havok/classes/Modifiers.h"
#include "havok/classes/gen/BSIStateManagerModifierBSiStateData.h"

#include <cstdint>
#include <vector>

namespace havok {

class BSIStateManagerModifier : public hkbModifier {
public:
    std::int32_t                                     m_iStateVar = 0;
    std::vector<BSIStateManagerModifierBSiStateData> m_stateData;
    HK_CLASS_ID(0x6cb24f2eu, "BSIStateManagerModifier")
};

} // namespace havok
