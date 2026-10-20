#pragma once

/// @file
/// @brief Layouts seeded from Havok's own reflection in the exe (tools/cbmatch --seed-headers): Havok's member names,
///        the gaps as explicit padding; tools/cbmatch --check-layout proves every named member.

#include <Common/Base/hkBase.h>
#include <Physics/Dynamics/Constraint/Atom/Bridge/hkpBridgeConstraintAtom.h>
#include <Physics/Dynamics/Constraint/hkpConstraintData.h>

namespace CB { namespace havok
{
    class hkpParametricCurve;

    /// @brief Seeded from Havok's reflection (size 0xc0).
    class hkpPointToPathConstraintData : public hkpConstraintData
    {
    public:
        enum OrientationConstraintType
        {
            CONSTRAIN_ORIENTATION_INVALID = 0,
            CONSTRAIN_ORIENTATION_NONE = 1,
            CONSTRAIN_ORIENTATION_ALLOW_SPIN = 2,
            CONSTRAIN_ORIENTATION_TO_PATH = 3,
            CONSTRAIN_ORIENTATION_MAX_ID = 4,
        };
        hkpBridgeAtoms m_atoms;   ///< +0x18
        hkpParametricCurve* m_path;   ///< +0x30
        hkReal m_maxFrictionForce;   ///< +0x38
        hkInt8 m_angularConstrainedDOF;   ///< +0x3c
        hkTransform m_transform_OS_KS[2];   ///< +0x40
    };
} }
