/// @file
/// @brief hkpLinkedCollidable: a collidable plus the collision agents linking it to its partners.
#pragma once

#include <Physics/Collide/Agent/Collidable/hkpCollidable.h>

namespace CB { namespace schema
{
    /// @note 0x80 bytes (schema/physics/hkpLinkedCollidable.yaml).
    class hkpLinkedCollidable : public hkpCollidable
    {
    public:
        struct CollisionEntry
        {
            void*                m_agentEntry;   ///< +0x00 — hkpAgentNnEntry*
            hkpLinkedCollidable* m_partner;      ///< +0x08
        };

        hkArray<CollisionEntry> m_collisionEntries;   ///< +0x70
    };
} }
