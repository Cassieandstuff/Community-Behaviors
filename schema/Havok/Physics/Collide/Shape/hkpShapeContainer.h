#pragma once

/// @file
/// @brief Layouts seeded from Havok's own reflection in the exe (tools/cbmatch --seed-headers): Havok's member names,
///        the gaps as explicit padding; tools/cbmatch --check-layout proves every named member.

#include <Common/Base/hkBase.h>
#include <Physics/Collide/Shape/hkpShape.h>

namespace CB { namespace schema
{
    /// @brief Seeded from Havok's reflection (size 0x8).
    struct hkpShapeContainer
    {
    public:
        hkUint8 m_pad0x0[0x8];
    };

    /// @brief Seeded from Havok's reflection (size 0x10).
    struct hkpSingleShapeContainer : public hkpShapeContainer
    {
    public:
        hkpShape* m_childShape;   ///< +0x8
    };
} }
