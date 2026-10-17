/// @file
/// @brief hkpLinkedCollidable: a collidable plus the collision agents linking it to its partners.
#pragma once

#include <Physics/Collide/Agent/Collidable/hkpCollidable.h>

namespace CB { namespace schema
{
    struct hkpAgentNnEntry;

    /// @note 0x80 bytes (schema/physics/hkpLinkedCollidable.yaml).
    class hkpLinkedCollidable : public hkpCollidable
    {
    public:
        struct CollisionEntry
        {
            hkpAgentNnEntry*     m_agentEntry;   ///< +0x00
            hkpLinkedCollidable* m_partner;      ///< +0x08
        };

        hkArray<CollisionEntry> m_collisionEntries;   ///< +0x70

        /// @brief Copies the collision entries into `entriesOut`, sorted.
        /// @note Out of line in vanilla (hkp code, not reimplemented).
        void getCollisionEntriesSorted(hkArray<CollisionEntry>& entriesOut) const;
        /// @brief The collision entries in no particular order.
        /// @note Out of line in vanilla (hkp code, not reimplemented).
        const hkArray<CollisionEntry>& getCollisionEntriesNonDeterministic() const;
    };
} }
