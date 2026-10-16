/// @file
/// @brief hkpContactListener: told about an entity's contact points.
#pragma once

#include <Physics/Dynamics/Collide/ContactListener/hkpContactPointEvent.h>

namespace CB { namespace schema
{
    struct hkpCollisionEvent;
    struct hkpContactPointAddedEvent;
    struct hkpContactPointRemovedEvent;
    struct hkpContactProcessEvent;

    /// @note 8 bytes (the vtable, 0x1417ec570). Slots per the Havok SDK (the destructor is slot 3, as
    ///       BSRagdollContactListenerModifier's secondary vtable has it); the three after it are Havok's deprecated
    ///       callbacks. Every callback is empty unless overridden.
    class hkpContactListener
    {
    public:
        /// @brief A contact point between the listened entity and another body.
        virtual void contactPointCallback(const hkpContactPointEvent& event) {}            ///< slot 0
        virtual void collisionAddedCallback(const hkpCollisionEvent& event) {}             ///< slot 1
        virtual void collisionRemovedCallback(const hkpCollisionEvent& event) {}           ///< slot 2
        virtual ~hkpContactListener() {}                                                   ///< slot 3
        virtual void contactPointAddedCallback(hkpContactPointAddedEvent& event) {}        ///< slot 4
        virtual void contactPointRemovedCallback(hkpContactPointRemovedEvent& event) {}    ///< slot 5
        virtual void contactProcessCallback(hkpContactProcessEvent& event) {}              ///< slot 6
    };
} }
