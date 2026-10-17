/// @file
/// @brief hkpConstraintOwner and hkpSimulationIsland: the island an entity simulates in, which owns its constraints.
#pragma once

#include <Common/Base/hkBase.h>

namespace CB { namespace schema
{
    class hkpConstraintInstance;

    /// @brief What owns constraints (an island; the contact managers' hkCollisionConstraintOwner).
    /// @note Slot 6 is proven by hkpResponseModifier's call; 3..5 are the Havok SDK's, declared so it lands.
    class hkpConstraintOwner : public hkReferencedObject
    {
    public:
        virtual void addConstraintToCriticalLockedIsland(hkpConstraintInstance* constraint);        ///< slot 3 (unverified)
        virtual void removeConstraintFromCriticalLockedIsland(hkpConstraintInstance* constraint);   ///< slot 4 (unverified)
        virtual void addCallbackRequest(hkpConstraintInstance* constraint, int request);            ///< slot 5 (unverified)
        /// @brief Asserts the caller may write the owner (a no-op in release).
        virtual void checkAccessRw();                                                               ///< slot 6
    };

    /// @note 0xb8 bytes per Havok's reflection (which lists no members and skips the unreflected base); reached through
    ///       hkpEntity::m_simulationIsland (+0x130). None of its members are proven here.
    class hkpSimulationIsland : public hkpConstraintOwner
    {
    public:
        hkUint8 m_pad10[0xb8 - 0x10];
    };
} }
