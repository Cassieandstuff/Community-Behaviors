#pragma once

/// @file
/// @brief Layouts seeded from Havok's own reflection in the exe (tools/cbmatch --seed-headers): Havok's member names,
///        the gaps as explicit padding; tools/cbmatch --check-layout proves every named member.

#include <Common/Base/hkBase.h>
#include <Common/SceneData/Attributes/hkxAttributeHolder.h>
#include <Common/SceneData/Graph/hkxNode.h>

namespace CB { namespace schema
{
    /// @brief Seeded from Havok's reflection (size 0x38).
    class hkxNodeSelectionSet : public hkxAttributeHolder
    {
    public:
        hkArray<hkxNode*> m_selectedNodes;   ///< +0x20
        hkStringPtr m_name;   ///< +0x30
    };
} }
