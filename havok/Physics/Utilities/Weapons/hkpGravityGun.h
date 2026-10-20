#pragma once

/// @file
/// @brief Layouts seeded from Havok's own reflection in the exe (tools/cbmatch --seed-headers): Havok's member names,
///        the gaps as explicit padding; tools/cbmatch --check-layout proves every named member.
/// @note Written by hand in the seeder's form: Havok reflects an array here as pointers of no class, which the seeder
///       cannot type; it stands as hkArray<void*>.

#include <Common/Base/hkBase.h>
#include <Physics/Utilities/Weapons/hkpFirstPersonGun.h>

namespace CB { namespace havok
{
    /// @brief Seeded from Havok's reflection (size 0x80).
    class hkpGravityGun : public hkpFirstPersonGun
    {
    public:
        hkArray<void*> m_grabbedBodies;   ///< +0x38, not serialized
        hkInt32 m_maxNumObjectsPicked;   ///< +0x48
        hkReal m_maxMassOfObjectPicked;   ///< +0x4c
        hkReal m_maxDistOfObjectPicked;   ///< +0x50
        hkReal m_impulseAppliedWhenObjectNotPicked;   ///< +0x54
        hkReal m_throwVelocity;   ///< +0x58
        hkVector4 m_capturedObjectPosition;   ///< +0x60
        hkVector4 m_capturedObjectsOffset;   ///< +0x70
    };
} }
