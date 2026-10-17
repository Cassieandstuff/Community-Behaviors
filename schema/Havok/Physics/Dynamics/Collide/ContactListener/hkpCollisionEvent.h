/// @file
/// @brief hkpCollisionEvent: the pair of bodies a contact-listener callback is about.
#pragma once

#include <Common/Base/hkBase.h>

namespace CB { namespace schema
{
    class hkpRigidBody;

    /// @note 0x20 bytes; layout per the Havok SDK (m_bodies confirmed by BSRagdollContactListenerModifier).
    struct hkpCollisionEvent
    {
        /// @brief Which listener list the callback came from.
        enum CallbackSource
        {
            SOURCE_A     = 0,
            SOURCE_B     = 1,
            SOURCE_WORLD = 2,
        };

        CallbackSource m_source;       ///< +0x00
        hkpRigidBody*  m_bodies[2];    ///< +0x08
        /// @note An hkpSimpleConstraintContactMgr* (not declared).
        void*          m_contactMgr;   ///< +0x18
    };
} }
