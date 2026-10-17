#pragma once

/// @file
/// @brief Layouts seeded from Havok's own reflection in the exe (tools/cbmatch --seed-headers): Havok's member names,
///        the gaps as explicit padding; tools/cbmatch --check-layout proves every named member.

#include <Common/Base/hkBase.h>
#include <Physics/Dynamics/Action/hkpBinaryAction.h>

namespace CB { namespace schema
{
    /// @brief Seeded from Havok's reflection (size 0x80).
    class hkpSpringAction : public hkpBinaryAction
    {
    public:
        hkVector4 m_lastForce;   ///< +0x40
        hkVector4 m_positionAinA;   ///< +0x50
        hkVector4 m_positionBinB;   ///< +0x60
        hkReal m_restLength;   ///< +0x70
        hkReal m_strength;   ///< +0x74
        hkReal m_damping;   ///< +0x78
        hkBool m_onCompression;   ///< +0x7c
        hkBool m_onExtension;   ///< +0x7d
    };
} }
