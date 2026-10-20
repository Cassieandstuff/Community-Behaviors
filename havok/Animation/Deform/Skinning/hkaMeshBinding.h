/// @file
/// @brief hkaMeshBinding: a skinned mesh bound to a skeleton.
#pragma once

#include <Animation/Rig/hkaSkeleton.h>
#include <Common/SceneData/Mesh/hkxMesh.h>

namespace CB { namespace havok
{
    struct hkaMeshBinding : hkReferencedObject
    {
        /// @brief One mesh section's skin index → bone map.
        struct Mapping
        {
            hkArray<hkInt16> m_mapping;
        };

        hkRefPtr<hkxMesh>      m_mesh;                         ///< +0x10
        hkStringPtr            m_originalSkeletonName;         ///< +0x18
        hkRefPtr<hkaSkeleton>  m_skeleton;                     ///< +0x20
        hkArray<Mapping>       m_mappings;                     ///< +0x28
        hkArray<hkTransform>   m_boneFromSkinMeshTransforms;   ///< +0x38

        hkaMeshBinding();
        hkaMeshBinding(hkFinishLoadedObjectFlag flag);
        virtual ~hkaMeshBinding();
    };
} }
