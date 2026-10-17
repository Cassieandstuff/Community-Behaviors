#pragma once

/// @file
/// @brief Layouts seeded from Havok's own reflection in the exe (tools/cbmatch --seed-headers): Havok's member names,
///        the gaps as explicit padding; tools/cbmatch --check-layout proves every named member.

#include <Common/Base/Object/hkReferencedObject.h>
#include <Common/Base/hkBase.h>

namespace CB { namespace schema
{
    /// @brief Seeded from Havok's reflection (size 0x38, version 1).
    class hkxTextureInplace : public hkReferencedObject
    {
    public:
        char m_fileType[4];   ///< +0x10
        hkArray<hkUint8> m_data;   ///< +0x18
        hkStringPtr m_name;   ///< +0x28
        hkStringPtr m_originalFilename;   ///< +0x30
    };
} }
