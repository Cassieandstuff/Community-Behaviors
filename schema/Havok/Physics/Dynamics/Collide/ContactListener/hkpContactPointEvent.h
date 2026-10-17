/// @file
/// @brief hkpContactPointEvent: one contact point reported to a contact listener.
#pragma once

#include <Physics/Collide/Agent/Query/hkpCdPointCollector.h>
#include <Physics/Dynamics/Collide/ContactListener/hkpCollisionEvent.h>

namespace CB { namespace schema
{
    /// @note Only the members behavior code reads are declared; layout per the Havok SDK (m_contactPoint confirmed by
    ///       BSRagdollContactListenerModifier).
    struct hkpContactPointEvent : hkpCollisionEvent
    {
        int             m_type;           ///< +0x20 — the Havok SDK's hkpContactPointEvent::Type
        hkContactPoint* m_contactPoint;   ///< +0x28
    };
} }
