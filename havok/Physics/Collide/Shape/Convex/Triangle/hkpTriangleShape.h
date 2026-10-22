#pragma once

/// @file
/// @brief Layouts seeded from Havok's own reflection in the exe (tools/cbmatch --seed-headers): Havok's member names,
///        the gaps as explicit padding; tools/cbmatch --check-layout proves every named member.

#include <Common/Base/hkBase.h>
#include <Physics/Collide/Shape/Convex/hkpConvexShape.h>

namespace CB { namespace havok
{
    /// @brief Seeded from Havok's reflection (size 0x70).
    class hkpTriangleShape : public hkpConvexShape
    {
    public:
        hkUint16 m_weldingInfo;   ///< +0x28
        hkUint8 m_weldingType;   ///< +0x2a
        hkUint8 m_isExtruded;   ///< +0x2b
        hkVector4 m_vertexA;   ///< +0x30
        hkVector4 m_vertexB;   ///< +0x40
        hkVector4 m_vertexC;   ///< +0x50
        hkVector4 m_extrusion;   ///< +0x60
    };
} }
