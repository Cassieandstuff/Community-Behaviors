/// @file
/// @brief hkpRigidBody: the entity behavior code reads and drives (transform, velocities, motion type, filter info).
#pragma once

#include <Physics/Dynamics/Entity/hkpEntity.h>

namespace CB { namespace schema
{
    /// @note 0x2d0 bytes, no members of its own (schema/physics/hkpRigidBody.yaml). Every accessor reads the embedded
    ///       motion (+0x150): motion type +0x160, transform +0x170, position +0x1a0, rotation +0x1e0, linear velocity
    ///       +0x230, angular velocity +0x240.
    class hkpRigidBody : public hkpEntity
    {
    public:
        virtual hkpRigidBody* clone() const;   ///< slot 7

        /// @brief Switches the body to `newState` (a hkpMotion::MotionType), activating it per `preferredActivationState`
        ///        (an hkpEntityActivation).
        /// @note Out of line in vanilla (hkp code, not reimplemented); parameters per the Havok SDK.
        void setMotionType(int newState, int preferredActivationState = HK_ENTITY_ACTIVATION_DO_ACTIVATE);
        /// @brief Teleports the body (its centre of mass follows) without touching its velocities.
        /// @note Out of line in vanilla (hkp code, not reimplemented).
        void setPositionAndRotation(const hkVector4& position, const hkQuaternion& rotation);

        HK_FORCE_INLINE hkpMotion*             getRigidMotion() const { return const_cast<hkpMaxSizeMotion*>(&m_motion); }
        HK_FORCE_INLINE hkpMotion::MotionType  getMotionType() const { return m_motion.getType(); }
        HK_FORCE_INLINE const hkTransform&     getTransform() const { return m_motion.getTransform(); }
        HK_FORCE_INLINE const hkVector4&       getPosition() const { return m_motion.getPosition(); }
        HK_FORCE_INLINE const hkQuaternion&    getRotation() const { return m_motion.getRotation(); }
        HK_FORCE_INLINE const hkVector4&       getCenterOfMassInWorld() const { return m_motion.getCenterOfMassInWorld(); }
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
    };
} }
