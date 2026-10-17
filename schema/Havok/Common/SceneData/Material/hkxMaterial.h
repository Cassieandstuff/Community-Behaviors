#pragma once

/// @file
/// @brief Layouts seeded from Havok's own reflection in the exe (tools/cbmatch --seed-headers): Havok's member names,
///        the gaps as explicit padding; tools/cbmatch --check-layout proves every named member.

#include <Common/Base/Object/hkReferencedObject.h>
#include <Common/Base/hkBase.h>
#include <Common/SceneData/Attributes/hkxAttributeHolder.h>

namespace CB { namespace schema
{
    /// @brief Seeded from Havok's reflection (size 0xb0, version 1).
    class hkxMaterial : public hkxAttributeHolder
    {
    public:
        /// @brief Seeded from Havok's reflection (size 0x10).
        struct TextureStage
        {
        public:
            hkReferencedObject* m_texture;   ///< +0x0
            hkInt32 m_usageHint;   ///< +0x8
            hkInt32 m_tcoordChannel;   ///< +0xc
        };

        /// @brief Seeded from Havok's reflection (size 0x8).
        struct Property
        {
        public:
            hkUint32 m_key;   ///< +0x0
            hkUint32 m_value;   ///< +0x4
        };

        enum TextureType
        {
            TEX_UNKNOWN = 0,
            TEX_DIFFUSE = 1,
            TEX_REFLECTION = 2,
            TEX_BUMP = 3,
            TEX_NORMAL = 4,
            TEX_DISPLACEMENT = 5,
            TEX_SPECULAR = 6,
            TEX_SPECULARANDGLOSS = 7,
            TEX_OPACITY = 8,
            TEX_EMISSIVE = 9,
            TEX_REFRACTION = 10,
            TEX_GLOSS = 11,
            TEX_NOTEXPORTED = 12,
        };
        enum PropertyKey
        {
            PROPERTY_MTL_TYPE_BLEND = 1,
            PROPERTY_MTL_UV_ID_STAGE0 = 256,
            PROPERTY_MTL_UV_ID_STAGE1 = 257,
            PROPERTY_MTL_UV_ID_STAGE2 = 258,
            PROPERTY_MTL_UV_ID_STAGE3 = 259,
            PROPERTY_MTL_UV_ID_STAGE4 = 260,
            PROPERTY_MTL_UV_ID_STAGE5 = 261,
            PROPERTY_MTL_UV_ID_STAGE6 = 262,
            PROPERTY_MTL_UV_ID_STAGE7 = 263,
            PROPERTY_MTL_UV_ID_STAGE8 = 264,
            PROPERTY_MTL_UV_ID_STAGE9 = 265,
            PROPERTY_MTL_UV_ID_STAGE10 = 266,
            PROPERTY_MTL_UV_ID_STAGE11 = 267,
            PROPERTY_MTL_UV_ID_STAGE12 = 268,
            PROPERTY_MTL_UV_ID_STAGE13 = 269,
            PROPERTY_MTL_UV_ID_STAGE14 = 270,
            PROPERTY_MTL_UV_ID_STAGE15 = 271,
            PROPERTY_MTL_UV_ID_STAGE_MAX = 272,
        };
        hkStringPtr m_name;   ///< +0x20
        hkArray<TextureStage> m_stages;   ///< +0x28
        hkVector4 m_diffuseColor;   ///< +0x40
        hkVector4 m_ambientColor;   ///< +0x50
        hkVector4 m_specularColor;   ///< +0x60
        hkVector4 m_emissiveColor;   ///< +0x70
        hkArray<hkxMaterial*> m_subMaterials;   ///< +0x80
        hkReferencedObject* m_extraData;   ///< +0x90
        hkArray<Property> m_properties;   ///< +0x98
    };
} }
