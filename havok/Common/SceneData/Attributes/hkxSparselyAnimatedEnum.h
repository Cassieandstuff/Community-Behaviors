#pragma once

/// @file
/// @brief Layouts seeded from Havok's own reflection in the exe (tools/cbmatch --seed-headers): Havok's member names,
///        the gaps as explicit padding; tools/cbmatch --check-layout proves every named member.

#include <Common/Base/hkBase.h>
#include <Common/SceneData/Attributes/hkxSparselyAnimatedInt.h>

namespace CB { namespace havok
{
    class hkxEnum;

    /// @brief Seeded from Havok's reflection (size 0x38, version 1).
    class hkxSparselyAnimatedEnum : public hkxSparselyAnimatedInt
    {
    public:
        hkxEnum* m_enum;   ///< +0x30
    };
} }
