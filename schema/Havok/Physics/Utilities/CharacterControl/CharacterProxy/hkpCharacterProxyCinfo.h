/// @file
/// @brief hkpCharacterProxyCinfo: the construction info of a character proxy.
#pragma once

#include <Common/Base/hkBase.h>

namespace CB { namespace schema
{
    class hkpShapePhantom;

    /// @note 0x90 bytes; vtable 0x1419654e0. hkbProxyModifier builds one on the stack and fills every field.
    class hkpCharacterProxyCinfo : public hkReferencedObject
    {
    public:
        hkVector4        m_position;                        ///< +0x10
        hkVector4        m_velocity;                        ///< +0x20
        hkReal           m_dynamicFriction;                 ///< +0x30
        hkReal           m_staticFriction;                  ///< +0x34
        hkReal           m_keepContactTolerance;            ///< +0x38
        hkVector4        m_up;                              ///< +0x40
        hkReal           m_extraUpStaticFriction;           ///< +0x50
        hkReal           m_extraDownStaticFriction;         ///< +0x54
        hkpShapePhantom* m_shapePhantom;                    ///< +0x58
        hkReal           m_keepDistance;                    ///< +0x60
        hkReal           m_contactAngleSensitivity;         ///< +0x64
        hkUint32         m_userPlanes;                      ///< +0x68
        hkReal           m_maxCharacterSpeedForSolver;      ///< +0x6c
        hkReal           m_characterStrength;               ///< +0x70
        hkReal           m_characterMass;                   ///< +0x74
        /// @brief Radians.
        hkReal           m_maxSlope;                        ///< +0x78
        hkReal           m_penetrationRecoverySpeed;        ///< +0x7c
        int              m_maxCastIterations;               ///< +0x80
        hkBool           m_refreshManifoldInCheckSupport;   ///< +0x84
    };
} }
