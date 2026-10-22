#pragma once

/// @file
/// @brief Layouts seeded from Havok's own reflection in the exe (tools/cbmatch --seed-headers): Havok's member names,
///        the gaps as explicit padding; tools/cbmatch --check-layout proves every named member.

#include <Common/Base/hkBase.h>
#include <Physics/Utilities/Weapons/hkpFirstPersonGun.h>

namespace CB { namespace havok
{
    /// @brief Seeded from Havok's reflection (size 0x70).
    class hkpBallGun : public hkpFirstPersonGun
    {
    public:
        hkReal m_bulletRadius;   ///< +0x38
        hkReal m_bulletVelocity;   ///< +0x3c
        hkReal m_bulletMass;   ///< +0x40
        hkReal m_damageMultiplier;   ///< +0x44
        hkInt32 m_maxBulletsInWorld;   ///< +0x48
        hkVector4 m_bulletOffsetFromCenter;   ///< +0x50
        void* m_addedBodies;   ///< +0x60, not serialized
    };
} }
