#pragma once

/// @file
/// @brief Layouts seeded from Havok's own reflection in the exe (tools/cbmatch --seed-headers): Havok's member names,
///        the gaps as explicit padding; tools/cbmatch --check-layout proves every named member.

#include <Common/Base/hkBase.h>
#include <Physics/Collide/Shape/Convex/ConvexTransform/hkpConvexTransformShape.h>

namespace CB { namespace schema
{
    /// @brief Seeded from Havok's reflection (size 0x50).
    class hkpConvexTranslateShape : public hkpConvexTransformShapeBase
    {
    public:
        hkVector4 m_translation;   ///< +0x40
    };
} }
