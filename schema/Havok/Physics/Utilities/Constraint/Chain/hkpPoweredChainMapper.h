#pragma once

/// @file
/// @brief Layouts seeded from Havok's own reflection in the exe (tools/cbmatch --seed-headers): Havok's member names,
///        the gaps as explicit padding; tools/cbmatch --check-layout proves every named member.

#include <Common/Base/Object/hkReferencedObject.h>
#include <Common/Base/hkBase.h>
#include <Physics/Dynamics/Constraint/hkpConstraintInstance.h>

namespace CB { namespace schema
{
    class hkpConstraintChainInstance;
    class hkpPoweredChainData;

    /// @brief Seeded from Havok's reflection (size 0x40).
    class hkpPoweredChainMapper : public hkReferencedObject
    {
    public:
        /// @brief Seeded from Havok's reflection (size 0x10).
        struct LinkInfo
        {
        public:
            hkInt32 m_firstTargetIdx;   ///< +0x0
            hkInt32 m_numTargets;   ///< +0x4
            hkpConstraintInstance* m_limitConstraint;   ///< +0x8
        };

        /// @brief Seeded from Havok's reflection (size 0x10).
        struct Target
        {
        public:
            hkpPoweredChainData* m_chain;   ///< +0x0
            hkInt32 m_infoIndex;   ///< +0x8
        };

        hkArray<LinkInfo> m_links;   ///< +0x10
        hkArray<Target> m_targets;   ///< +0x20
        hkArray<hkpConstraintChainInstance*> m_chains;   ///< +0x30
    };
} }
