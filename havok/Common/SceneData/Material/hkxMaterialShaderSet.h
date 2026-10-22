#pragma once

/// @file
/// @brief Layouts seeded from Havok's own reflection in the exe (tools/cbmatch --seed-headers): Havok's member names,
///        the gaps as explicit padding; tools/cbmatch --check-layout proves every named member.

#include <Common/Base/Object/hkReferencedObject.h>
#include <Common/Base/hkBase.h>

namespace CB { namespace havok
{
    class hkxMaterialShader;

    /// @brief Seeded from Havok's reflection (size 0x20, version 1).
    class hkxMaterialShaderSet : public hkReferencedObject
    {
    public:
        hkArray<hkxMaterialShader*> m_shaders;   ///< +0x10
    };
} }
