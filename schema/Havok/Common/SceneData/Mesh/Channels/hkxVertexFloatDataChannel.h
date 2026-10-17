#pragma once

/// @file
/// @brief Layouts seeded from Havok's own reflection in the exe (tools/cbmatch --seed-headers): Havok's member names,
///        the gaps as explicit padding; tools/cbmatch --check-layout proves every named member.

#include <Common/Base/Object/hkReferencedObject.h>
#include <Common/Base/hkBase.h>

namespace CB { namespace schema
{
    /// @brief Seeded from Havok's reflection (size 0x28, version 1).
    class hkxVertexFloatDataChannel : public hkReferencedObject
    {
    public:
        enum VertexFloatDimensions
        {
            FLOAT = 0,
            DISTANCE = 1,
            ANGLE = 2,
        };
        hkArray<hkReal> m_perVertexFloats;   ///< +0x10
        hkUint8 m_dimensions;   ///< +0x20
    };
} }
