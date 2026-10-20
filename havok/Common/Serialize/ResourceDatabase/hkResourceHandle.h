#pragma once

/// @file
/// @brief Layouts seeded from Havok's own reflection in the exe (tools/cbmatch --seed-headers): Havok's member names,
///        the gaps as explicit padding; tools/cbmatch --check-layout proves every named member.

#include <Common/Base/Object/hkReferencedObject.h>
#include <Common/Base/hkBase.h>
#include <Common/Serialize/Resource/hkResource.h>

namespace CB { namespace havok
{
    /// @brief Seeded from Havok's reflection (size 0x30, version 2).
    class hkMemoryResourceHandle : public hkResourceHandle
    {
    public:
        /// @brief Seeded from Havok's reflection, which names it hkMemoryResourceHandleExternalLink (size 0x10, version 1).
        struct ExternalLink
        {
            hkStringPtr m_memberName;   ///< +0x0
            hkStringPtr m_externalId;   ///< +0x8
        };

        hkReferencedObject* m_variant;   ///< +0x10
        hkStringPtr m_name;   ///< +0x18
        hkArray<ExternalLink> m_references;   ///< +0x20
    };

    /// @brief Seeded from Havok's reflection (size 0x40, version 1).
    class hkMemoryResourceContainer : public hkResourceContainer
    {
    public:
        hkStringPtr m_name;   ///< +0x10
        hkMemoryResourceContainer* m_parent;   ///< +0x18, not serialized
        hkArray<hkMemoryResourceHandle*> m_resourceHandles;   ///< +0x20
        hkArray<hkMemoryResourceContainer*> m_children;   ///< +0x30
    };
} }
