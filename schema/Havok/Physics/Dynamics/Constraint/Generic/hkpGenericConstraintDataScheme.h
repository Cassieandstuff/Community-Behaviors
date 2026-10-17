#pragma once

/// @file
/// @brief Layouts seeded from Havok's own reflection in the exe (tools/cbmatch --seed-headers): Havok's member names,
///        the gaps as explicit padding; tools/cbmatch --check-layout proves every named member.
/// @note Written by hand in the seeder's form: Havok reflects an array here as pointers of no class, which the seeder
///       cannot type; it stands as hkArray<void*>.

#include <Common/Base/hkBase.h>
#include <Physics/Dynamics/Constraint/Motor/hkpConstraintMotor.h>

namespace CB { namespace schema
{
    /// @brief Seeded from Havok's reflection (size 0x50).
    struct hkpGenericConstraintDataScheme
    {
    public:
        /// @brief Seeded from Havok's reflection (size 0x10).
        struct ConstraintInfo
        {
        public:
            hkInt32 m_maxSizeOfSchema;   ///< +0x0
            hkInt32 m_sizeOfSchemas;   ///< +0x4
            hkInt32 m_numSolverResults;   ///< +0x8
            hkInt32 m_numSolverElemTemps;   ///< +0xc
        };

        ConstraintInfo m_info;   ///< +0x0, not serialized
        hkArray<hkVector4> m_data;   ///< +0x10
        hkArray<hkInt32> m_commands;   ///< +0x20
        hkArray<void*> m_modifiers;   ///< +0x30, not serialized
        hkArray<hkpConstraintMotor*> m_motors;   ///< +0x40
    };
} }
