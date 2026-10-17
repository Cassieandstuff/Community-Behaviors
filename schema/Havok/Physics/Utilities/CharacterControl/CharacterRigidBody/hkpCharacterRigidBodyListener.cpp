/// @file
/// @brief hkpCharacterRigidBodyListener, reimplemented against vanilla (AE 1.6.1170).
#include <Physics/Utilities/CharacterControl/CharacterRigidBody/hkpCharacterRigidBodyListener.h>

namespace CB { namespace schema
{
    void hkpCharacterRigidBodyListener::processActualPoints(const hkpWorld* world, hkpCharacterRigidBody* characterRB,
                                                            const hkpLinkedCollidable::CollisionEntry& entry, hkpSimpleConstraintContactMgr* mgr,
                                                            hkArray<hkContactPointId>& contactPointIds)
    {
        unweldContactPoints(characterRB, entry, mgr, contactPointIds);
        considerCollisionEntryForSlope(world, characterRB, entry, mgr, contactPointIds);
        considerCollisionEntryForMassModification(world, characterRB, entry, mgr, contactPointIds);
    }
} }
