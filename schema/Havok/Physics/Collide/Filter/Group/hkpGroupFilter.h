/// @file
/// @brief hkpCollisionFilter / hkpGroupFilter: only what the behavior runtime calls.
#pragma once

#include <Common/Base/hkBase.h>

namespace CB { namespace schema
{
    /// @note The type tag (+0x38) is all that is read; 2 is the Havok SDK's HK_FILTER_GROUP.
    class hkpCollisionFilter : public hkReferencedObject
    {
    public:
        enum hkpFilterType { HK_FILTER_UNKNOWN = 0, HK_FILTER_NULL = 1, HK_FILTER_GROUP = 2 };

        char    m_pad10[0x38 - 0x10];
        hkInt32 m_type;   ///< +0x38 — hkpFilterType
    };

    class hkpGroupFilter : public hkpCollisionFilter
    {
    public:
        /// @brief Whether objects with these collision filter infos (layer + system group) may collide.
        /// @note Out of line in vanilla; the hkBool comes back through memory.
        hkBool isCollisionEnabled(hkUint32 infoA, hkUint32 infoB) const;
    };
} }
