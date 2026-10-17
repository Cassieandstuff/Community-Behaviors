#pragma once

/// @file
/// @brief Layouts seeded from Havok's own reflection in the exe (tools/cbmatch --seed-headers): Havok's member names,
///        the gaps as explicit padding; tools/cbmatch --check-layout proves every named member.
/// @note Written by hand in the seeder's form: Havok reflects an array here as pointers of no class, which the seeder
///       cannot type; it stands as hkArray<void*>.

#include <Common/Base/hkBase.h>
#include <Physics/Utilities/Weapons/hkpFirstPersonGun.h>

namespace CB { namespace schema
{
    /// @brief Seeded from Havok's reflection (size 0x68).
    class hkpProjectileGun : public hkpFirstPersonGun
    {
    public:
        hkInt32 m_maxProjectiles;   ///< +0x38
        hkReal m_reloadTime;   ///< +0x3c
        hkReal m_reload;   ///< +0x40, not serialized
        hkArray<void*> m_projectiles;   ///< +0x48, not serialized
        void* m_world;   ///< +0x58, not serialized
        void* m_destructionWorld;   ///< +0x60, not serialized
    };
} }
