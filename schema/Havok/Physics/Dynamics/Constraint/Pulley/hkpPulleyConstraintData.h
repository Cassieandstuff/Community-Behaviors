#pragma once

/// @file
/// @brief Layouts seeded from Havok's own reflection in the exe (tools/cbmatch --seed-headers): Havok's member names,
///        the gaps as explicit padding; tools/cbmatch --check-layout proves every named member.

#include <Common/Base/hkBase.h>
#include <Physics/Dynamics/Constraint/Atom/hkpConstraintAtom.h>
#include <Physics/Dynamics/Constraint/hkpConstraintData.h>

namespace CB { namespace schema
{
    /// @brief Seeded from Havok's reflection (size 0x90).
    class hkpPulleyConstraintData : public hkpConstraintData
    {
    public:
        /// @brief Seeded from Havok's reflection (size 0x70).
        struct Atoms
        {
        public:
            hkpSetLocalTranslationsConstraintAtom m_translations;   ///< +0x0
            hkpPulleyConstraintAtom m_pulley;   ///< +0x30
        };

        Atoms m_atoms;   ///< +0x20
    };
} }
