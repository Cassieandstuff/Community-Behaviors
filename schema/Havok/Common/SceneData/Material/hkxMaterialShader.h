#pragma once

/// @file
/// @brief Layouts seeded from Havok's own reflection in the exe (tools/cbmatch --seed-headers): Havok's member names,
///        the gaps as explicit padding; tools/cbmatch --check-layout proves every named member.

#include <Common/Base/Object/hkReferencedObject.h>
#include <Common/Base/hkBase.h>

namespace CB { namespace schema
{
    /// @brief Seeded from Havok's reflection (size 0x48, version 1).
    class hkxMaterialShader : public hkReferencedObject
    {
    public:
        enum ShaderType
        {
            EFFECT_TYPE_INVALID = 0,
            EFFECT_TYPE_UNKNOWN = 1,
            EFFECT_TYPE_HLSL_INLINE = 2,
            EFFECT_TYPE_CG_INLINE = 3,
            EFFECT_TYPE_HLSL_FILENAME = 4,
            EFFECT_TYPE_CG_FILENAME = 5,
            EFFECT_TYPE_MAX_ID = 6,
        };
        hkStringPtr m_name;   ///< +0x10
        hkUint8 m_type;   ///< +0x18
        hkStringPtr m_vertexEntryName;   ///< +0x20
        hkStringPtr m_geomEntryName;   ///< +0x28
        hkStringPtr m_pixelEntryName;   ///< +0x30
        hkArray<hkUint8> m_data;   ///< +0x38
    };
} }
