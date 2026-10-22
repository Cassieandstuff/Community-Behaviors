/// @file
/// @brief BSTimerModifier: Bethesda's timer — raises an event once a set time has elapsed, optionally re-arming.
#pragma once

#include <Behavior/Context/hkbContext.h>
#include <Behavior/Event/hkbEventQueue.h>
#include <Behavior/Modifier/hkbModifier.h>

namespace CB { namespace havok
{
    /// @note Member names are the class's reflected names (m_secondsElapsed is reflected serialize-ignored). The
    ///       `slot N` annotations are vanilla's vtable slots. The destructor is implicit: the scalar deleting
    ///       destructor (slot 0) inlines it.
    class BSTimerModifier : public hkbModifier
    {
    public:
        hkReal           m_alarmTimeSeconds;   ///< +0x50
        hkbEventProperty m_alarmEvent;         ///< +0x58
        /// @brief When set the alarm re-arms: the elapsed time drops by the alarm time each time it fires.
        hkBool           m_resetAlarm;         ///< +0x68
        hkReal           m_secondsElapsed;     ///< +0x6c

        /// @note Both constructors (and the finish-loaded one) set bit 1 of m_userData; what it flags is unknown
        ///       (hkbTwistModifier sets bit 0 the same way).
        BSTimerModifier();
        BSTimerModifier(hkFinishLoadedObjectFlag flag);
        /// @brief The cloneNode copy: unlike hkbTimerModifier's, it keeps the elapsed time.
        BSTimerModifier(const BSTimerModifier& mod);

        static const hkClass& staticClass();

        virtual const void* getClassType() const;                                ///< slot 1
        virtual void        activate(const hkbContext& context);                 ///< slot 4
        virtual void        update(const hkbContext& context, hkReal timestep);  ///< slot 5
        virtual hkbNode*    cloneNode(hkbBehaviorGraph& rootBehavior) const;     ///< slot 12
    };
} }
