#pragma once

/// @file
/// @brief Layouts seeded from Havok's own reflection in the exe (tools/cbmatch --seed-headers): Havok's member names,
///        the gaps as explicit padding; tools/cbmatch --check-layout proves every named member.

#include <Common/Base/hkBase.h>
#include <Physics/Dynamics/Constraint/Bilateral/PointToPath/hkpParametricCurve.h>

namespace CB { namespace schema
{
    /// @brief Seeded from Havok's reflection (size 0x50).
    class hkpLinearParametricCurve : public hkpParametricCurve
    {
    public:
        hkReal m_smoothingFactor;   ///< +0x10
        hkBool m_closedLoop;   ///< +0x14
        hkVector4 m_dirNotParallelToTangentAlongWholePath;   ///< +0x20
        hkArray<hkVector4> m_points;   ///< +0x30
        hkArray<hkReal> m_distance;   ///< +0x40
    };
} }
