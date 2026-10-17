/// @file
/// @brief hkxMesh: a scene mesh (sections of vertex/index buffers).
#pragma once

#include <Common/Base/hkBase.h>

namespace CB { namespace schema
{
    class hkxMeshSection;

    class hkxMesh : public hkReferencedObject
    {
    public:
        class UserChannelInfo;

        hkArray<hkRefPtr<hkxMeshSection>>  m_sections;           ///< +0x10
        hkArray<hkRefPtr<UserChannelInfo>> m_userChannelInfos;   ///< +0x20
    };
} }
