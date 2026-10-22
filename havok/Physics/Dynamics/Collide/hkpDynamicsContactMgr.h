/// @file
/// @brief hkpDynamicsContactMgr: a contact manager whose points live in the dynamics (a contact constraint).
#pragma once

#include <Physics/Collide/Agent/ContactMgr/hkpContactMgr.h>
#include <Physics/Dynamics/Constraint/Contact/hkpContactPointProperties.h>

namespace CB { namespace havok
{
    class hkpConstraintInstance;

    /// @note Slots 10..12 proven by hkpCharacterRigidBodyListener, 16 by hkpResponseModifier; 13..15 are the Havok SDK's,
    ///       declared so slot 16 lands. Not reflected.
    class hkpDynamicsContactMgr : public hkpContactMgr
    {
    public:
        virtual hkpContactPointProperties* getContactPointProperties(hkContactPointId id) = 0;   ///< slot 10
        virtual hkContactPoint*            getContactPoint(hkContactPointId id) = 0;             ///< slot 11
        virtual void                       getAllContactPointIds(hkArray<hkContactPointId>& contactPointIds) const = 0;   ///< slot 12
        virtual int                        getType() const = 0;                                        ///< slot 13 (unverified)
        virtual void                       toiCollisionResponseBeginCallback(const void* event, void* constraintStepInfo) = 0;   ///< slot 14 (unverified)
        virtual void                       toiCollisionResponseEndCallback(const void* event, hkReal impulseApplied, void* constraintStepInfo) = 0;   ///< slot 15 (unverified)
        /// @brief The contact constraint the manager's points live in, or null.
        virtual hkpConstraintInstance*     getConstraintInstance() = 0;                                ///< slot 16
    };
} }
