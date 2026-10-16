/// @file
/// @brief hkpMotion: a body's dynamic state (motion state, velocities, mass, deactivation) and the motion-type hierarchy.
#pragma once

#include <Common/Base/Types/Physics/MotionState/hkMotionState.h>

namespace CB { namespace schema
{
    class hkpMaxSizeMotion;
    struct hkMatrix3;

    /// @note 0x140 bytes (schema/physics/hkpMotion.yaml). Virtual slots per the Havok SDK; 16 (setLinearVelocity) and 17
    ///       (setAngularVelocity) are confirmed by hkaRagdollInstance, the rest are not. Havok's pure virtuals are
    ///       declared plain: hkpEntity embeds the motion by value, which an abstract class cannot be.
    class hkpMotion : public hkReferencedObject
    {
    public:
        /// @note Values per the Havok SDK; MOTION_KEYFRAMED (4) is confirmed by the attachment modifier.
        enum MotionType
        {
            MOTION_INVALID           = 0,
            MOTION_DYNAMIC           = 1,
            MOTION_SPHERE_INERTIA    = 2,
            MOTION_BOX_INERTIA       = 3,
            MOTION_KEYFRAMED         = 4,
            MOTION_FIXED             = 5,
            MOTION_THIN_BOX_INERTIA  = 6,
            MOTION_CHARACTER         = 7,
        };

        /// @note Read as an unsigned byte by vanilla (movzx).
        hkUint8           m_type;                               ///< +0x10 — MotionType
        hkUint8           m_deactivationIntegrateCounter;       ///< +0x11
        hkUint16          m_deactivationNumInactiveFrames[2];   ///< +0x12
        hkMotionState     m_motionState;                        ///< +0x20
        hkVector4         m_inertiaAndMassInv;                  ///< +0xd0
        hkVector4         m_linearVelocity;                     ///< +0xe0
        hkVector4         m_angularVelocity;                    ///< +0xf0
        hkVector4         m_deactivationRefPosition[2];         ///< +0x100
        hkUint32          m_deactivationRefOrientation[2];      ///< +0x120
        hkpMaxSizeMotion* m_savedMotion;                        ///< +0x128
        hkUint16          m_savedQualityTypeIndex;              ///< +0x130
        hkHalf            m_gravityFactor;                      ///< +0x132

        virtual void setMass(hkReal m);                                                    ///< slot 3
        virtual void setMassInv(hkReal mInv);                                              ///< slot 4
        virtual void getInertiaLocal(hkMatrix3& inertiaOut) const;                         ///< slot 5
        virtual void getInertiaWorld(hkMatrix3& inertiaOut) const;                         ///< slot 6
        virtual void setInertiaLocal(const hkMatrix3& inertia);                            ///< slot 7
        virtual void setInertiaInvLocal(const hkMatrix3& inertiaInv);                      ///< slot 8
        virtual void getInertiaInvLocal(hkMatrix3& inertiaInvOut) const;                   ///< slot 9
        virtual void getInertiaInvWorld(hkMatrix3& inertiaInvOut) const;                   ///< slot 10
        virtual void setCenterOfMassInLocal(const hkVector4& centerOfMass);                ///< slot 11
        virtual void setPosition(const hkVector4& position);                               ///< slot 12
        virtual void setRotation(const hkQuaternion& rotation);                            ///< slot 13
        virtual void setPositionAndRotation(const hkVector4& position, const hkQuaternion& rotation);   ///< slot 14
        virtual void setTransform(const hkTransform& transform);                           ///< slot 15
        virtual void setLinearVelocity(const hkVector4& newVel);                           ///< slot 16
        virtual void setAngularVelocity(const hkVector4& newVel);                          ///< slot 17

        HK_FORCE_INLINE MotionType           getType() const { return static_cast<MotionType>(m_type); }
        HK_FORCE_INLINE const hkMotionState* getMotionState() const { return &m_motionState; }
        HK_FORCE_INLINE hkMotionState*       getMotionState() { return &m_motionState; }
        HK_FORCE_INLINE const hkTransform&   getTransform() const { return m_motionState.getTransform(); }
        HK_FORCE_INLINE const hkVector4&     getPosition() const { return m_motionState.getTransform().getTranslation(); }
        HK_FORCE_INLINE const hkQuaternion&  getRotation() const { return m_motionState.getSweptTransform().m_rotation1; }
        HK_FORCE_INLINE const hkVector4&     getCenterOfMassInWorld() const { return m_motionState.getSweptTransform().m_centerOfMass1; }
        HK_FORCE_INLINE const hkVector4&     getCenterOfMassLocal() const { return m_motionState.getSweptTransform().m_centerOfMassLocal; }
        HK_FORCE_INLINE const hkVector4&     getLinearVelocity() const { return m_linearVelocity; }
        HK_FORCE_INLINE const hkVector4&     getAngularVelocity() const { return m_angularVelocity; }

        /// @brief The transform at `time`, interpolated (approximately) within the last step's swept transform.
        /// @note Out of line in vanilla (hkp code, not reimplemented).
        void approxTransformAt(hkTime time, hkTransform& transformOut) const;
    };
} }
