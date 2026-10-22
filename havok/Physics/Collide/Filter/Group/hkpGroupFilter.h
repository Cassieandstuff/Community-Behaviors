/// @file
/// @brief hkpCollisionFilter / hkpGroupFilter: only what the behavior runtime calls.
#pragma once

#include <Common/Base/hkBase.h>

namespace CB { namespace havok
{
    /// @note The type tag (+0x38) is all behavior code reads; 2 is the Havok SDK's HK_FILTER_GROUP.
    class hkpCollisionFilter : public hkReferencedObject
    {
    public:
        enum hkpFilterType { HK_FILTER_UNKNOWN = 0, HK_FILTER_NULL = 1, HK_FILTER_GROUP = 2 };

        char     m_pad10[0x30 - 0x10];
        hkUint32 m_prepad[2];    ///< +0x30
        hkInt32  m_type;         ///< +0x38 — hkpFilterType
        hkUint32 m_postpad[3];   ///< +0x3c
    };

    class hkpGroupFilter : public hkpCollisionFilter
    {
    public:
        hkInt32   m_nextFreeSystemGroup;        ///< +0x48
        hkUint32  m_collisionLookupTable[32];   ///< +0x4c
        hkVector4 m_pad256[4];                  ///< +0xd0

        /// @brief Whether objects with these collision filter infos (layer + system group) may collide.
        /// @note Out of line in vanilla; the hkBool comes back through memory.
        hkBool isCollisionEnabled(hkUint32 infoA, hkUint32 infoB) const;

        /// @brief A collision filter info: layer in bits 0-4, subsystem id 5-9, the subsystem it does not collide with
        ///        10-14, system group from bit 16.
        static HK_FORCE_INLINE hkUint32 calcFilterInfo(int layer, int systemGroup = 0, int subSystemId = 0, int subSystemDontCollideWith = 0)
        {
            return hkUint32((systemGroup << 16) | (subSystemDontCollideWith << 10) | (subSystemId << 5) | layer);
        }
        static HK_FORCE_INLINE int getLayerFromFilterInfo(hkUint32 filterInfo) { return filterInfo & 0x1f; }
        static HK_FORCE_INLINE int getSystemGroupFromFilterInfo(hkUint32 filterInfo) { return filterInfo >> 16; }
        static HK_FORCE_INLINE int getSubSystemIdFromFilterInfo(hkUint32 filterInfo) { return (filterInfo >> 5) & 0x1f; }
        static HK_FORCE_INLINE int getSubSystemDontCollideWithFromFilterInfo(hkUint32 filterInfo) { return (filterInfo >> 10) & 0x1f; }
    };
} }
