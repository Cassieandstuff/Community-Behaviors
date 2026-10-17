#pragma once

/// @file
/// @brief Layouts seeded from Havok's own reflection in the exe (tools/cbmatch --seed-headers): Havok's member names,
///        the gaps as explicit padding; tools/cbmatch --check-layout proves every named member.
/// @note Written by hand in the seeder's form: Havok reflects an array here as pointers of no class, which the seeder
///       cannot type; it stands as hkArray<void*>.

#include <Common/Base/hkBase.h>
#include <Physics/Dynamics/Action/hkpUnaryAction.h>

namespace CB { namespace schema
{
    /// @brief Seeded from Havok's reflection (size 0x90).
    class hkpMouseSpringAction : public hkpUnaryAction
    {
    public:
        hkVector4 m_positionInRbLocal;   ///< +0x40
        hkVector4 m_mousePositionInWorld;   ///< +0x50
        hkReal m_springDamping;   ///< +0x60
        hkReal m_springElasticity;   ///< +0x64
        hkReal m_maxRelativeForce;   ///< +0x68
        hkReal m_objectDamping;   ///< +0x6c
        hkUint32 m_shapeKey;   ///< +0x70
        hkArray<void*> m_applyCallbacks;   ///< +0x78, not serialized
    };
} }
