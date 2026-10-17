#pragma once

/// @file
/// @brief Layouts seeded from Havok's own reflection in the exe (tools/cbmatch --seed-headers): Havok's member names,
///        the gaps as explicit padding; tools/cbmatch --check-layout proves every named member.

#include <Common/Base/Object/hkReferencedObject.h>
#include <Common/Base/hkBase.h>
#include <Common/SceneData/Mesh/hkxMesh.h>

namespace CB { namespace schema
{
    /// @brief Seeded from Havok's reflection (size 0x80, version 2).
    class hkxSkinBinding : public hkReferencedObject
    {
    public:
        hkxMesh* m_mesh;   ///< +0x10
        hkArray<hkStringPtr> m_nodeNames;   ///< +0x18
        hkArray<hkMatrix4> m_bindPose;   ///< +0x28
        hkMatrix4 m_initSkinTransform;   ///< +0x40
    };
} }
