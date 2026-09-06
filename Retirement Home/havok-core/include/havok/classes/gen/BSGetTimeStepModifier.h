#pragma once
#include "havok/classes/IHavokObject.h"

// BSGetTimeStepModifier — Bethesda modifier that writes the frame time step into a
// variable. hkbModifier + m_timeStep (float). size 88, sig 0xbda33bfe. Used by dragon.

#include "havok/classes/Modifiers.h"

namespace havok {

class BSGetTimeStepModifier : public hkbModifier {
public:
    float m_timeStep = 0.f;
    HK_CLASS_ID(0xbda33bfeu, "BSGetTimeStepModifier")
};

} // namespace havok
