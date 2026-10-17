#pragma once

/// @file
/// @brief Layouts seeded from Havok's own reflection in the exe (tools/cbmatch --seed-headers): Havok's member names,
///        the gaps as explicit padding; tools/cbmatch --check-layout proves every named member.

#include <Common/Base/Object/hkReferencedObject.h>
#include <Common/Base/hkBase.h>

namespace CB { namespace schema
{
    /// @brief Seeded from Havok's reflection (size 0x30).
    class hkpConvexVerticesConnectivity : public hkReferencedObject
    {
    public:
        hkArray<hkUint16> m_vertexIndices;   ///< +0x10
        hkArray<hkUint8> m_numVerticesPerFace;   ///< +0x20
    };
} }
