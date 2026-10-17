#pragma once

/// @file
/// @brief Layouts seeded from Havok's own reflection in the exe (tools/cbmatch --seed-headers): Havok's member names,
///        the gaps as explicit padding; tools/cbmatch --check-layout proves every named member.
/// @note Written by hand in the seeder's form: Havok reflects an array here as pointers of no class, which the seeder
///       cannot type; it stands as hkArray<void*>.

#include <Common/Base/Object/hkReferencedObject.h>
#include <Common/Base/hkBase.h>

namespace CB { namespace schema
{
    /// @brief Seeded from Havok's reflection (size 0x38).
    class hkpFirstPersonGun : public hkReferencedObject
    {
    public:
        enum Type
        {
            WEAPON_TYPE_INVALID = 0,
            WEAPON_TYPE_BALLGUN = 1,
            WEAPON_TYPE_GRENADEGUN = 2,
            WEAPON_TYPE_GRAVITYGUN = 3,
            WEAPON_TYPE_MOUNTEDBALLGUN = 4,
            WEAPON_TYPE_TWEAKERGUN = 5,
            WEAPON_TYPE_MISSILEGUN = 6,
            WEAPON_TYPE_RAYCASTGUN = 7,
            WEAPON_TYPE_SPHEREGUN = 8,
            WEAPON_TYPE_STICKYGUN = 9,
            WEAPON_TYPE_NUM_TYPES = 10,
        };
        enum KeyboardKey
        {
            KEY_F1 = 112,
            KEY_F2 = 113,
            KEY_F3 = 114,
            KEY_F4 = 115,
            KEY_F5 = 116,
            KEY_F6 = 117,
            KEY_F7 = 118,
            KEY_F8 = 119,
            KEY_F9 = 120,
            KEY_F10 = 121,
            KEY_F11 = 122,
            KEY_F12 = 123,
        };
        hkEnum<Type, hkUint8> m_type;   ///< +0x10, not serialized
        hkStringPtr m_name;   ///< +0x18
        hkEnum<KeyboardKey, hkUint8> m_keyboardKey;   ///< +0x20
        hkArray<void*> m_listeners;   ///< +0x28, not serialized
    };
} }
