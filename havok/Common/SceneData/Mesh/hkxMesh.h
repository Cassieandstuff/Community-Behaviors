/// @file
/// @brief hkxMesh: a scene mesh (sections of vertex/index buffers).
#pragma once

#include <Common/Base/hkBase.h>
#include <Common/SceneData/Attributes/hkxAttributeHolder.h>

namespace CB { namespace havok
{
    class hkxMeshSection;

    class hkxMesh : public hkReferencedObject
    {
    public:
        class UserChannelInfo;

        hkArray<hkRefPtr<hkxMeshSection>>  m_sections;           ///< +0x10
        hkArray<hkRefPtr<UserChannelInfo>> m_userChannelInfos;   ///< +0x20
    };

    /// @brief Seeded from Havok's reflection (size 0x30).
    class hkxMesh::UserChannelInfo : public hkxAttributeHolder
    {
    public:
        hkStringPtr m_name;   ///< +0x20
        hkStringPtr m_className;   ///< +0x28
    };
} }
