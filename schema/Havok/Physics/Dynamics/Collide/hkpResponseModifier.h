/// @file
/// @brief hkpResponseModifier: per-contact solver tweaks (mass scaling and the like) applied through a contact
///        manager's constraint.
#pragma once

#include <Common/Base/hkBase.h>

namespace CB { namespace schema
{
    class hkpDynamicsContactMgr;
    class hkpRigidBody;
    class hkpConstraintOwner;

    class hkpResponseModifier
    {
    public:
        /// @brief Scales `body`'s inverse mass (xyz: inertia, w: mass) by `factor` in the contacts `manager` holds.
        static void setInvMassScalingForContact(hkpDynamicsContactMgr* manager, hkpRigidBody* body, hkpConstraintOwner& constraintOwner,
                                                const hkVector4& factor);
    };
} }
