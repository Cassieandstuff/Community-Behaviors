/// @file
/// @brief hkbTimerModifier: raises an event once a set time has elapsed.
#pragma once

#include <Behavior/Context/hkbContext.h>
#include <Behavior/Event/hkbEventQueue.h>
#include <Behavior/Modifier/hkbModifier.h>

namespace CB { namespace schema
{
    struct hkbTimerModifierInternalState : hkReferencedObject
    {
        hkReal m_secondsElapsed;   ///< +0x10
    };

    /// @note The `slot N` annotations are vanilla's vtable slots (25 of them; the rest are hkbModifier's). The destructor
    ///       is implicit: vanilla's scalar deleting destructor (slot 0) inlines it.
    class hkbTimerModifier : public hkbModifier
    {
    public:
        hkReal           m_alarmTimeSeconds;   ///< +0x50
        hkbEventProperty m_alarmEvent;         ///< +0x58
        hkReal           m_secondsElapsed;     ///< +0x68

        /// @brief The cloneNode copy: the settings, with the elapsed time restarted.
        hkbTimerModifier(const hkbTimerModifier& mod);

        virtual void                activate(const hkbContext& context);                              ///< slot 4
        virtual void                update(const hkbContext& context, hkReal timestep);               ///< slot 5
        virtual hkbNode*            cloneNode(hkbBehaviorGraph& rootBehavior) const;                  ///< slot 12
        virtual hkReferencedObject* createInternalState();                                            ///< slot 13
        /// @note Slots 14 and 16 take Havok's names for the internal-state family that follows createInternalState
        ///       (get, getUser, set, setUser); the timer overrides the two plain ones.
        virtual void                getInternalState(hkReferencedObject& internalState) const;       ///< slot 14
        virtual void                setInternalState(const hkReferencedObject& internalState);       ///< slot 16
    };
} }
