#pragma once

/// @file
/// @brief Layouts seeded from Havok's own reflection in the exe (tools/cbmatch --seed-headers): Havok's member names,
///        the gaps as explicit padding; tools/cbmatch --check-layout proves every named member.

#include <Common/Base/hkBase.h>
#include <Physics/Collide/Shape/Compound/Tree/hkpBvTreeShape.h>
#include <Physics/Collide/Shape/hkpShapeContainer.h>

namespace CB { namespace havok
{
    class hkpMoppCode;

    /// @brief Seeded from Havok's reflection (size 0x50).
    class hkMoppBvTreeShapeBase : public hkpBvTreeShape
    {
    public:
        hkpMoppCode* m_code;   ///< +0x28
        void* m_moppData;   ///< +0x30, not serialized
        hkUint32 m_moppDataSize;   ///< +0x38, not serialized
        hkVector4 m_codeInfoCopy;   ///< +0x40, not serialized
    };

    /// @brief Seeded from Havok's reflection (size 0x70).
    class hkpMoppBvTreeShape : public hkMoppBvTreeShapeBase
    {
    public:
        hkpSingleShapeContainer m_child;   ///< +0x50
        hkInt32 m_childSize;   ///< +0x60, not serialized
    };
} }
