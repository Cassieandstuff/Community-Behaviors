#pragma once

/// @file
/// @brief Layouts seeded from Havok's own reflection in the exe (tools/cbmatch --seed-headers): Havok's member names,
///        the gaps as explicit padding; tools/cbmatch --check-layout proves every named member.

#include <Common/Base/Object/hkReferencedObject.h>
#include <Common/Base/hkBase.h>

namespace CB { namespace havok
{
    /// @brief Seeded from Havok's reflection (size 0x10).
    struct hkpMoppCodeCodeInfo
    {
    public:
        hkVector4 m_offset;   ///< +0x0
    };

    /// @brief Seeded from Havok's reflection (size 0x40).
    class hkpMoppCode : public hkReferencedObject
    {
    public:
        /// @brief Seeded from Havok's reflection (size 0x8).
        struct ReindexedTerminal
        {
        public:
            hkUint32 m_origShapeKey;   ///< +0x0
            hkUint32 m_reindexedShapeKey;   ///< +0x4
        };

        enum BuildType
        {
            BUILT_WITH_CHUNK_SUBDIVISION = 0,
            BUILT_WITHOUT_CHUNK_SUBDIVISION = 1,
            BUILD_NOT_SET = 2,
        };
        hkpMoppCodeCodeInfo m_info;   ///< +0x10
        hkArray<hkUint8> m_data;   ///< +0x20
        hkInt8 m_buildType;   ///< +0x30
    };
} }
