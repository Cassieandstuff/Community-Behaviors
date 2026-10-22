#pragma once

/// @file
/// @brief Layouts seeded from Havok's own reflection in the exe (tools/cbmatch --seed-headers): Havok's member names,
///        the gaps as explicit padding; tools/cbmatch --check-layout proves every named member.

#include <Common/Base/hkBase.h>

namespace CB { namespace havok
{
    /// @brief Seeded from Havok's reflection (size 0x20).
    struct hkGeometry
    {
    public:
        /// @brief Seeded from Havok's reflection (size 0x10).
        struct Triangle
        {
        public:
            hkInt32 m_a;   ///< +0x0
            hkInt32 m_b;   ///< +0x4
            hkInt32 m_c;   ///< +0x8
            hkInt32 m_material;   ///< +0xc
        };

        hkArray<hkVector4> m_vertices;   ///< +0x0
        hkArray<Triangle> m_triangles;   ///< +0x10
    };
} }
