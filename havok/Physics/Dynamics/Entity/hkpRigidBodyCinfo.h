/// @file
/// @brief hkpRigidBodyCinfo: the construction info of an hkpRigidBody.
#pragma once

#include <Physics/Dynamics/Motion/hkpMotion.h>
#include <Physics/Collide/Agent/Collidable/hkpCollidable.h>

namespace CB { namespace havok
{
    class hkpShape;

    /// @note Not reflected. Only the members hkpCharacterRigidBody's constructor sets are declared; the size is
    ///       vanilla's stack slot for it (the frame proves it is at most 0xe0).
    class hkpRigidBodyCinfo
    {
    public:
        hkUint32        m_collisionFilterInfo;       ///< +0x00
        const hkpShape* m_shape;                     ///< +0x08
        hkUint8         m_pad10[0x20 - 0x10];
        hkVector4       m_position;                  ///< +0x20
        hkQuaternion    m_rotation;                  ///< +0x30
        hkUint8         m_pad40[0xa0 - 0x40];
        hkReal          m_mass;                      ///< +0xa0
        hkUint8         m_padA4[0xb4 - 0xa4];
        hkReal          m_friction;                  ///< +0xb4
        hkUint8         m_padB8[0xbc - 0xb8];
        hkReal          m_restitution;               ///< +0xbc
        hkReal          m_maxLinearVelocity;         ///< +0xc0
        hkUint8         m_padC4[0xc8 - 0xc4];
        hkReal          m_allowedPenetrationDepth;   ///< +0xc8
        hkInt8          m_motionType;                ///< +0xcc — hkpMotion::MotionType
        hkUint8         m_padCD[0xcf - 0xcd];
        hkInt8          m_qualityType;               ///< +0xcf — hkpCollidableQualityType
        hkUint8         m_padD0[0xe0 - 0xd0];

        /// @brief Havok's defaults.
        /// @note Out of line in vanilla (hkp code, not reimplemented).
        hkpRigidBodyCinfo();
    };
} }
