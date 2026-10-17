/// @file
/// @brief hkpCharacterRigidBodyListener: the contact filter of the rigid-body character controller (the controller
///        Skyrim drives actors with, bUseCharacterRB). Per contact entry it unwelds edge normals, adds vertical planes
///        for contacts steeper than maxSlope, and modifies masses.
#pragma once

#include <Common/Base/hkBase.h>
#include <Physics/Collide/Agent3/Machine/Nn/hkpLinkedCollidable.h>
#include <Physics/Collide/Agent/ContactMgr/hkpContactMgr.h>

namespace CB { namespace schema
{
    class hkpWorld;
    class hkpCharacterRigidBody;
    class hkpSimpleConstraintContactMgr;

    /// @note Vtable per vanilla 0x1419ee5e8: slots 3..7 below (proven by processActualPoints' calls).
    class hkpCharacterRigidBodyListener : public hkReferencedObject
    {
    public:
        virtual void characterCallback(hkpWorld* world, hkpCharacterRigidBody* characterRB);
        virtual void processActualPoints(const hkpWorld* world, hkpCharacterRigidBody* characterRB, const hkpLinkedCollidable::CollisionEntry& entry,
                                         hkpSimpleConstraintContactMgr* mgr, hkArray<hkContactPointId>& contactPointIds);
        virtual void unweldContactPoints(hkpCharacterRigidBody* characterRB, const hkpLinkedCollidable::CollisionEntry& entry,
                                         hkpSimpleConstraintContactMgr* mgr, const hkArray<hkContactPointId>& contactPointIds);
        virtual void considerCollisionEntryForSlope(const hkpWorld* world, hkpCharacterRigidBody* characterRB,
                                                    const hkpLinkedCollidable::CollisionEntry& entry, hkpSimpleConstraintContactMgr* mgr,
                                                    hkArray<hkContactPointId>& contactPointIds);
        virtual void considerCollisionEntryForMassModification(const hkpWorld* world, hkpCharacterRigidBody* characterRB,
                                                               const hkpLinkedCollidable::CollisionEntry& entry,
                                                               hkpSimpleConstraintContactMgr* mgr, const hkArray<hkContactPointId>& contactPointIds);

        /// @brief Removes the vertical points the last step added (each found again among its manager's contacts by
        ///        an exact position and normal match) and empties the list.
        void discardVerticalPoints(hkpCharacterRigidBody* characterRB);
        /// @brief Runs the per-entry processActualPoints over every simple-constraint contact manager of the character
        ///        that holds contact points.
        void processActualPoints(hkpWorld* world, hkpCharacterRigidBody* characterRB);
    };
} }
