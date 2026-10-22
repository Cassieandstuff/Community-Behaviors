#pragma once

/// @file
/// @brief Layouts seeded from Havok's own reflection in the exe (tools/cbmatch --seed-headers): Havok's member names,
///        the gaps as explicit padding; tools/cbmatch --check-layout proves every named member.

#include <Common/Base/hkBase.h>
#include <Physics/Dynamics/Motion/Rigid/hkpKeyframedRigidMotion.h>

namespace CB { namespace havok
{
    /// @brief Seeded from Havok's reflection (size 0x140).
    class hkpFixedRigidMotion : public hkpKeyframedRigidMotion
    {
    public:
    };
} }
