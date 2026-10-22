#pragma once

/// @file
/// @brief Layouts seeded from Havok's own reflection in the exe (tools/cbmatch --seed-headers): Havok's member names,
///        the gaps as explicit padding; tools/cbmatch --check-layout proves every named member.

#include <Common/Base/Object/hkReferencedObject.h>
#include <Common/Base/hkBase.h>
#include <Physics/Dynamics/Entity/hkpRigidBody.h>

namespace CB { namespace havok
{
    /// @brief Seeded from Havok's reflection (size 0x58).
    class hkpTriggerVolume : public hkReferencedObject
    {
    public:
        /// @brief Seeded from Havok's reflection (size 0x18).
        struct EventInfo
        {
        public:
            hkUlong m_sortValue;   ///< +0x0
            hkpRigidBody* m_body;   ///< +0x8
            hkInt32 m_operation;   ///< +0x10
        };

        enum EventType
        {
            ENTERED_EVENT = 1,
            LEFT_EVENT = 2,
            ENTERED_AND_LEFT_EVENT = 3,
            TRIGGER_BODY_LEFT_EVENT = 6,
        };
        enum Operation
        {
            ADDED_OP = 0,
            REMOVED_OP = 1,
            CONTACT_OP = 2,
            TOI_OP = 3,
        };
        hkUint8 m_pad0x10[0x18];
        hkArray<hkpRigidBody*> m_overlappingBodies;   ///< +0x28
        hkArray<EventInfo> m_eventQueue;   ///< +0x38
        hkpRigidBody* m_triggerBody;   ///< +0x48
        hkUint32 m_sequenceNumber;   ///< +0x50
    };
} }
