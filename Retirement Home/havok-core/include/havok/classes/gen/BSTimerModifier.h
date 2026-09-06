#pragma once
#include "havok/classes/IHavokObject.h"

// BSTimerModifier — counts up to alarmTimeSeconds then fires alarmEvent. hkbModifier +
// alarmTimeSeconds + alarmEvent(hkbEventProperty) + resetAlarm(bool). Trailing
// secondsElapsed (float) is SERIALIZE_IGNORED runtime state. size 112, sig 0x531f3292.
// Used by dragon. (Distinct from hkbTimerModifier, which has no resetAlarm.)

#include "havok/classes/Modifiers.h"
#include "havok/classes/Events.h"

namespace havok {

class BSTimerModifier : public hkbModifier {
public:
    float            m_alarmTimeSeconds = 0.f;
    hkbEventProperty m_alarmEvent{};   // inline struct
    bool             m_resetAlarm = false;   // +108 secondsElapsed (ignored)
    HK_CLASS_ID(0x531f3292u, "BSTimerModifier")
};

} // namespace havok
