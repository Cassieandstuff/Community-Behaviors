/// @file
/// @brief hkbClipTrigger and hkbClipTriggerArray: the events a clip raises at points in its timeline.
#pragma once

#include <Behavior/Event/hkbEvent.h>

namespace CB { namespace schema
{
    /// @note 0x20 bytes.
    struct hkbClipTrigger
    {
        hkReal           m_localTime;             ///< +0x00
        hkbEventProperty m_event;                 ///< +0x08
        hkBool           m_relativeToEndOfClip;   ///< +0x18
        hkBool           m_acyclic;               ///< +0x19 — fires once on elapsed clip time, not on the loop
        hkBool           m_isAnnotation;          ///< +0x1a

        HK_FORCE_INLINE hkbClipTrigger(hkReal localTime, const hkbEventBase& event, bool relativeToEndOfClip, bool acyclic, bool isAnnotation)
        :   m_localTime(localTime), m_event(event), m_relativeToEndOfClip(relativeToEndOfClip), m_acyclic(acyclic), m_isAnnotation(isAnnotation) {}
    };

    struct hkbClipTriggerArray : hkReferencedObject
    {
        hkArray<hkbClipTrigger> m_triggers;   ///< +0x10

        HK_FORCE_INLINE hkbClipTriggerArray() {}
        hkbClipTriggerArray(const hkbClipTriggerArray& other);
    };
} }
