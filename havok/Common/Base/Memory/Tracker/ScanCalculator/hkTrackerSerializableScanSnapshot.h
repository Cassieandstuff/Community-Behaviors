#pragma once

/// @file
/// @brief Layouts seeded from Havok's own reflection in the exe (tools/cbmatch --seed-headers): Havok's member names,
///        the gaps as explicit padding; tools/cbmatch --check-layout proves every named member.

#include <Common/Base/Object/hkReferencedObject.h>
#include <Common/Base/hkBase.h>

namespace CB { namespace havok
{
    /// @brief Seeded from Havok's reflection (size 0x80).
    class hkTrackerSerializableScanSnapshot : public hkReferencedObject
    {
    public:
        /// @brief Seeded from Havok's reflection (size 0x18).
        struct Allocation
        {
        public:
            hkUlong m_start;   ///< +0x0
            hkUlong m_size;   ///< +0x8
            hkInt32 m_traceId;   ///< +0x10
        };

        /// @brief Seeded from Havok's reflection (size 0x28).
        struct Block
        {
        public:
            hkInt32 m_typeIndex;   ///< +0x0
            hkUlong m_start;   ///< +0x8
            hkUlong m_size;   ///< +0x10
            hkInt32 m_arraySize;   ///< +0x18
            hkInt32 m_startReferenceIndex;   ///< +0x1c
            hkInt32 m_numReferences;   ///< +0x20
        };

        hkArray<Allocation> m_allocations;   ///< +0x10
        hkArray<Block> m_blocks;   ///< +0x20
        hkArray<hkInt32> m_refs;   ///< +0x30
        hkArray<hkUint8> m_typeNames;   ///< +0x40
        hkArray<hkUint8> m_traceText;   ///< +0x50
        hkArray<hkUlong> m_traceAddrs;   ///< +0x60
        hkArray<hkInt32> m_traceParents;   ///< +0x70
    };
} }
