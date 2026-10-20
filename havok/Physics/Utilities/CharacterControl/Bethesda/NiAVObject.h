/// @file
/// @brief NiAVObject: Gamebryo's scene-graph object (its transforms and bound), and the lookup from a Havok object to
///        its node.
/// @note Not Havok: declared only as far as the character controller's code reads it.
#pragma once

#include <Physics/Utilities/CharacterControl/Bethesda/NiObject.h>
#include <Physics/Utilities/CharacterControl/Bethesda/NiMatrix3.h>
#include <Physics/Utilities/CharacterControl/Bethesda/BSTEventSource.h>

namespace CB { namespace havok
{
    class hkpWorldObject;

    /// @brief A rotation, translation (game units) and uniform scale.
    /// @note 0x34 bytes.
    struct NiTransform
    {
        NiMatrix3 m_rotate;      ///< +0x00
        NiPoint3  m_translate;   ///< +0x24
        float     m_scale;       ///< +0x30
    };

    /// @brief A bounding sphere, in game units.
    /// @note 0x10 bytes.
    struct NiBound
    {
        NiPoint3 m_center;   ///< +0x00
        float    m_radius;   ///< +0x0c
    };

    /// @note Opaque but for the members read here (offsets per CommonLibSSE: local +0x48, world +0x7c, previous world
    ///       +0xb0, world bound +0xe4).
    class NiAVObject : public NiObject
    {
    public:
        hkUint8     m_pad10[0x7c - 0x10];
        NiTransform m_world;        ///< +0x7c
        hkUint8     m_padb0[0xe4 - 0xb0];
        NiBound     m_worldBound;   ///< +0xe4
    };

    /// @brief The node `object` belongs to: its property `key` (1 when `key` is 0), null without one.
    /// @note Name provisional; out of line in vanilla (0x140e827d0), not reimplemented.
    NiAVObject* bhkGetNiObject(const hkpWorldObject* object, hkUint32 key);
} }
