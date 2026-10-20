/// @file
/// @brief hkaMeshBinding, reimplemented against vanilla (AE 1.6.1170).
#include <Animation/Deform/Skinning/hkaMeshBinding.h>

namespace CB { namespace havok
{
    hkaMeshBinding::hkaMeshBinding()
    {
    }

    hkaMeshBinding::~hkaMeshBinding()
    {
    }

    hkaMeshBinding::hkaMeshBinding(hkFinishLoadedObjectFlag flag)
    :   hkReferencedObject(flag),
        m_mesh(flag),
        m_originalSkeletonName(flag),
        m_skeleton(flag),
        m_mappings(flag),
        m_boneFromSkinMeshTransforms(flag)
    {
    }
} }
