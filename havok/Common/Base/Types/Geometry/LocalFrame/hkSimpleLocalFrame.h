#pragma once

/// @file
/// @brief Layouts seeded from Havok's own reflection in the exe (tools/cbmatch --seed-headers): Havok's member names,
///        the gaps as explicit padding; tools/cbmatch --check-layout proves every named member.

#include <Common/Base/Types/Geometry/LocalFrame/hkLocalFrame.h>
#include <Common/Base/hkBase.h>

namespace CB { namespace havok
{
    /// @brief Seeded from Havok's reflection (size 0x80, version 1).
    class hkSimpleLocalFrame : public hkLocalFrame
    {
    public:
        hkTransform m_transform;   ///< +0x10
        hkArray<hkLocalFrame*> m_children;   ///< +0x50
        hkLocalFrame* m_parentFrame;   ///< +0x60
        hkLocalFrameGroup* m_group;   ///< +0x68
        hkStringPtr m_name;   ///< +0x70
    };
} }
