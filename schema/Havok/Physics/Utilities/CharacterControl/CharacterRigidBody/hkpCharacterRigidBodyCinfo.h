/// @file
/// @brief hkpCharacterRigidBodyCinfo: the construction info of an hkpCharacterRigidBody.
#pragma once

#include <Common/Base/hkBase.h>
#include <Physics/Collide/Shape/hkpShape.h>
#include <Physics/Utilities/CharacterControl/hkpCharacterControl.h>

namespace CB { namespace schema
{
    /// @brief Seeded from Havok's reflection (size 0x80).
    class hkpCharacterRigidBodyCinfo : public hkpCharacterControllerCinfo
    {
    public:
        hkUint32     m_collisionFilterInfo;           ///< +0x10
        hkpShape*    m_shape;                         ///< +0x18
        hkVector4    m_position;                      ///< +0x20
        hkQuaternion m_rotation;                      ///< +0x30
        hkReal       m_mass;                          ///< +0x40
        hkReal       m_friction;                      ///< +0x44
        hkReal       m_maxLinearVelocity;             ///< +0x48
        hkReal       m_allowedPenetrationDepth;       ///< +0x4c
        hkVector4    m_up;                            ///< +0x50
        /// @brief Radians; the controller keeps its cosine.
        hkReal       m_maxSlope;                      ///< +0x60
        hkReal       m_maxForce;                      ///< +0x64
        hkReal       m_unweldingHeightOffsetFactor;   ///< +0x68
        hkReal       m_maxSpeedForSimplexSolver;      ///< +0x6c
        hkReal       m_supportDistance;               ///< +0x70
        hkReal       m_hardSupportDistance;           ///< +0x74
        hkInt32      m_vdbColor;                      ///< +0x78
    };
} }
