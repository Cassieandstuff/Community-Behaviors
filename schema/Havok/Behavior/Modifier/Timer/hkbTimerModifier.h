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

    struct hkbTimerModifier : hkbModifier
    {
        hkReal           m_alarmTimeSeconds;   ///< +0x50
        hkbEventProperty m_alarmEvent;         ///< +0x58
        hkReal           m_secondsElapsed;     ///< +0x68

        virtual void                update(const hkbContext& context, hkReal timestep);
        virtual hkReferencedObject* createInternalState();
    };
} }
