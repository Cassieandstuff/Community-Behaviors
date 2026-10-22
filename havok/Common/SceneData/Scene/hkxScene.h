#pragma once

/// @file
/// @brief Layouts seeded from Havok's own reflection in the exe (tools/cbmatch --seed-headers): Havok's member names,
///        the gaps as explicit padding; tools/cbmatch --check-layout proves every named member.

#include <Common/Base/Object/hkReferencedObject.h>
#include <Common/Base/hkBase.h>
#include <Common/SceneData/Mesh/hkxMesh.h>

namespace CB { namespace havok
{
    class hkxCamera;
    class hkxLight;
    class hkxMaterial;
    class hkxNode;
    class hkxNodeSelectionSet;
    class hkxSkinBinding;
    class hkxTextureFile;
    class hkxTextureInplace;

    /// @brief Seeded from Havok's reflection (size 0xe0, version 1).
    class hkxScene : public hkReferencedObject
    {
    public:
        hkStringPtr m_modeller;   ///< +0x10
        hkStringPtr m_asset;   ///< +0x18
        hkReal m_sceneLength;   ///< +0x20
        hkxNode* m_rootNode;   ///< +0x28
        hkArray<hkxNodeSelectionSet*> m_selectionSets;   ///< +0x30
        hkArray<hkxCamera*> m_cameras;   ///< +0x40
        hkArray<hkxLight*> m_lights;   ///< +0x50
        hkArray<hkxMesh*> m_meshes;   ///< +0x60
        hkArray<hkxMaterial*> m_materials;   ///< +0x70
        hkArray<hkxTextureInplace*> m_inplaceTextures;   ///< +0x80
        hkArray<hkxTextureFile*> m_externalTextures;   ///< +0x90
        hkArray<hkxSkinBinding*> m_skinBindings;   ///< +0xa0
        hkVector4 m_pad0xb0[3];   ///< appliedTransform (MATRIX3, a type the schema lacks)
    };
} }
