#pragma once

/// @file
/// @brief Layouts seeded from Havok's own reflection in the exe (tools/cbmatch --seed-headers): Havok's member names,
///        the gaps as explicit padding; tools/cbmatch --check-layout proves every named member.

#include <Common/Base/Object/hkReferencedObject.h>
#include <Common/Base/hkBase.h>

namespace CB { namespace havok
{
    /// @brief Seeded from Havok's reflection (size 0x30, version 1).
    class hkxMaterialEffect : public hkReferencedObject
    {
    public:
        enum EffectType
        {
            EFFECT_TYPE_INVALID = 0,
            EFFECT_TYPE_UNKNOWN = 1,
            EFFECT_TYPE_HLSL_FX_INLINE = 2,
            EFFECT_TYPE_CG_FX_INLINE = 3,
            EFFECT_TYPE_HLSL_FX_FILENAME = 4,
            EFFECT_TYPE_CG_FX_FILENAME = 5,
            EFFECT_TYPE_MAX_ID = 6,
        };
        hkStringPtr m_name;   ///< +0x10
        hkUint8 m_type;   ///< +0x18
        hkArray<hkUint8> m_data;   ///< +0x20
    };
} }
