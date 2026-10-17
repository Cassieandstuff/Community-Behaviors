#pragma once

/// @file
/// @brief Layouts seeded from Havok's own reflection in the exe (tools/cbmatch --seed-headers): Havok's member names,
///        the gaps as explicit padding; tools/cbmatch --check-layout proves every named member.

#include <Common/Base/Object/hkReferencedObject.h>
#include <Common/Base/hkBase.h>

namespace CB { namespace schema
{
    /// @brief Seeded from Havok's reflection (size 0x28, version 2).
    class hkAlignSceneToNodeOptions : public hkReferencedObject
    {
    public:
        hkBool m_invert;   ///< +0x10
        hkBool m_transformPositionX;   ///< +0x11
        hkBool m_transformPositionY;   ///< +0x12
        hkBool m_transformPositionZ;   ///< +0x13
        hkBool m_transformRotation;   ///< +0x14
        hkBool m_transformScale;   ///< +0x15
        hkBool m_transformSkew;   ///< +0x16
        hkInt32 m_keyframe;   ///< +0x18
        hkStringPtr m_nodeName;   ///< +0x20
    };
} }
