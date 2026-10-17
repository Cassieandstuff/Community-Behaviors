/// @file
/// @brief hkpRigidBody: the entity behavior code reads and drives (transform, velocities, motion type, filter info).
#pragma once

#include <Physics/Dynamics/Entity/hkpEntity.h>
#include <Physics/Dynamics/Entity/hkpRigidBodyCinfo.h>

namespace CB { namespace schema
{
    /// @note 0x2d0 bytes, no members of its own (schema/physics/hkpRigidBody.yaml). Every accessor reads the embedded
    ///       motion (+0x150): motion type +0x160, transform +0x170, position +0x1a0, rotation +0x1e0, linear velocity
    ///       +0x230, angular velocity +0x240.
    class hkpRigidBody : public hkpEntity
    {
    public:
        /// @note Out of line in vanilla (hkp code, not reimplemented).
        hkpRigidBody(const hkpRigidBodyCinfo& info);

        virtual hkpRigidBody* clone() const;   ///< slot 7

        /// @brief Switches the body to `newState` (a hkpMotion::MotionType), activating it per `preferredActivationState`
        ///        (an hkpEntityActivation).
        /// @note Out of line in vanilla (hkp code, not reimplemented); parameters per the Havok SDK.
        void setMotionType(int newState, int preferredActivationState = HK_ENTITY_ACTIVATION_DO_ACTIVATE);
        /// @brief Teleports the body (its centre of mass follows) without touching its velocities.
        /// @note Out of line in vanilla (hkp code, not reimplemented).
        void setPositionAndRotation(const hkVector4& position, const hkQuaternion& rotation);
        /// @brief Sets the inverse inertia in the body's space (a zero matrix: infinite inertia, the body never rotates).
        /// @note Out of line in vanilla (hkp code, not reimplemented): forwards to the motion's slot 8.
        void setInertiaInvLocal(const hkMatrix3& inertiaInv);
        /// @brief Teleports the body to `transform` without touching its velocities.
        /// @note Out of line in vanilla (hkp code, not reimplemented).
        void setTransform(const hkTransform& transform);
        /// @brief Teleports the body (its centre of mass follows) to `position` without touching its velocities.
        /// @note Out of line in vanilla (hkp code, not reimplemented).
        void setPosition(const hkVector4& position);
        /// @brief Sets the friction of the body's material.
        /// @note Out of line in vanilla (hkp code, not reimplemented).
        void setFriction(hkReal newFriction);

        HK_FORCE_INLINE hkpMotion*             getRigidMotion() const { return const_cast<hkpMaxSizeMotion*>(&m_motion); }
        HK_FORCE_INLINE hkpMotion::MotionType  getMotionType() const { return m_motion.getType(); }
        HK_FORCE_INLINE const hkTransform&     getTransform() const { return m_motion.getTransform(); }
        HK_FORCE_INLINE const hkVector4&       getPosition() const { return m_motion.getPosition(); }
        HK_FORCE_INLINE const hkQuaternion&    getRotation() const { return m_motion.getRotation(); }
        HK_FORCE_INLINE const hkVector4&       getCenterOfMassInWorld() const { return m_motion.getCenterOfMassInWorld(); }
        HK_FORCE_INLINE const hkVector4&       getCenterOfMassLocal() const { return m_motion.getCenterOfMassLocal(); }
        HK_FORCE_INLINE hkReal                 getMaxAngularVelocity() const { return m_motion.getMotionState()->getMaxAngularVelocity(); }
        HK_FORCE_INLINE const hkVector4&       getLinearVelocity() const { return m_motion.getLinearVelocity(); }
        HK_FORCE_INLINE const hkVector4&       getAngularVelocity() const { return m_motion.getAngularVelocity(); }
        HK_FORCE_INLINE hkUint32               getCollisionFilterInfo() const { return m_collidable.getCollisionFilterInfo(); }
        HK_FORCE_INLINE hkpCollidableQualityType getQualityType() const { return m_collidable.getQualityType(); }

        /// @note activate() then the motion's virtual (slot 16), as vanilla inlines it.
        HK_FORCE_INLINE void setLinearVelocity(const hkVector4& newVel)
        {
            activate();
            getRigidMotion()->setLinearVelocity(newVel);
        }
        /// @note activate() then the motion's virtual (slot 17), as vanilla inlines it.
        HK_FORCE_INLINE void setAngularVelocity(const hkVector4& newVel)
        {
            activate();
            getRigidMotion()->setAngularVelocity(newVel);
        }
        HK_FORCE_INLINE void approxTransformAt(hkTime time, hkTransform& transformOut) const { getRigidMotion()->approxTransformAt(time, transformOut); }
        HK_FORCE_INLINE hkReal getMass() const { return getRigidMotion()->getMass(); }
        /// @brief The velocity of the body's material at world point `p`: w x (p - centre of mass) + v.
        HK_FORCE_INLINE void getPointVelocity(const hkVector4& p, hkVector4& vecOut) const
        {
            hkVector4 relPos;
            relPos.setSub4(p, getCenterOfMassInWorld());
            vecOut.setCross(getAngularVelocity(), relPos);
            vecOut.add4(getLinearVelocity());
        }
    };

    /// @brief The rigid body owning `collidable`, null for a phantom or a broad-phase border.
    HK_FORCE_INLINE hkpRigidBody* hkpGetRigidBody(const hkpCollidable* collidable)
    {
        if (collidable->getType() == hkpCollidable::BROAD_PHASE_ENTITY) return static_cast<hkpRigidBody*>(static_cast<hkpWorldObject*>(collidable->getOwner()));
        return HK_NULL;
    }
} }
