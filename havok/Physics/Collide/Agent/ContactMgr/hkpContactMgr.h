/// @file
/// @brief hkpContactMgr: what a collision agent reports contact points to.
#pragma once

#include <Common/Base/hkBase.h>
#include <Physics/Collide/Agent/Query/hkpCdPointCollector.h>

namespace CB { namespace havok
{
    class hkpCdBody;
    class hkpCollidable;
    class hkpConstraintOwner;
    class hkpProcessCollisionData;
    class hkpProcessCollisionOutput;
    class hkpContactPointProperties;
    struct hkpProcessCollisionInput;
    struct hkpGskCache;
    typedef hkUint16 hkContactPointId;

    /// @brief The id addContactPoint returns when it rejects the point.
    enum { HK_INVALID_CONTACT_POINT = 0xffff };

    /// @note Slots 3 (addContactPoint) and 5 (removeContactPoint) are proven by hkpCharacterRigidBodyListener; 4 and
    ///       6..9 are the Havok SDK's, unverified, declared so the slots after them land. Not reflected.
    class hkpContactMgr : public hkReferencedObject
    {
    public:
        /// @note Only the simple-constraint manager's value is proven (hkpCharacterRigidBodyListener tests it before
        ///       treating a manager as one); the rest of the Havok SDK's list is not declared.
        enum Type { TYPE_SIMPLE_CONSTRAINT_CONTACT_MGR = 0 };

        hkEnum<Type, hkInt32> m_type;   ///< +0x10

        virtual hkContactPointId addContactPoint(const hkpCdBody& a, const hkpCdBody& b, const hkpProcessCollisionInput& input,
                                                 hkpProcessCollisionOutput& output, const hkpGskCache* contactCache, hkContactPoint& cp) = 0;   ///< slot 3
        virtual int  reserveContactPoints(int numPoints) = 0;                                                                         ///< slot 4
        virtual void removeContactPoint(hkContactPointId cpId, hkpConstraintOwner& constraintOwner) = 0;                              ///< slot 5
        virtual void processContact(const hkpCollidable& a, const hkpCollidable& b, const hkpProcessCollisionInput& input,
                                    hkpProcessCollisionData& collisionData) = 0;                                                      ///< slot 6
        virtual int  addToi(const hkpCdBody& a, const hkpCdBody& b, const hkpProcessCollisionInput& input, hkpProcessCollisionOutput& output,
                            hkTime toi, hkContactPoint& cp, const hkpGskCache* gskCache, hkReal& projectedVelocity,
                            hkpContactPointProperties& propertiesOut) = 0;                                                            ///< slot 7
        virtual void removeToi(hkpConstraintOwner& constraintOwner, hkpContactPointProperties& properties) = 0;                       ///< slot 8
        virtual void cleanup() = 0;                                                                                                   ///< slot 9
    };
} }
