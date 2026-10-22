/// @file
/// @brief hkpCharacterRigidBodyCinfo: the construction info of an hkpCharacterRigidBody.
#pragma once

#include <Common/Base/hkBase.h>
#include <Physics/Collide/Shape/hkpShape.h>
#include <Physics/Utilities/CharacterControl/hkpCharacterControl.h>

namespace CB { namespace havok
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

        /// @note Havok's defaults, as Bethesda's inlined copies store them.
        HK_FORCE_INLINE hkpCharacterRigidBodyCinfo()
        {
            m_mass                        = 100.0f;
            m_maxForce                    = 1000.0f;
            m_friction                    = 0.0f;
            m_maxSlope                    = HK_REAL_PI / 3.0f;
            m_unweldingHeightOffsetFactor = 0.5f;
            m_up.set(0.0f, 1.0f, 0.0f);
            m_maxLinearVelocity           = 20.0f;
            m_allowedPenetrationDepth     = -0.1f;
            m_maxSpeedForSimplexSolver    = 10.0f;
            m_collisionFilterInfo         = 0;
            m_position.setZero4();
            m_rotation.setIdentity();
            m_supportDistance             = 0.1f;
            m_hardSupportDistance         = 0.0f;
            m_vdbColor                    = static_cast<hkInt32>(0xA0FF0000);
            m_shape                       = HK_NULL;
        }
    };
} }
