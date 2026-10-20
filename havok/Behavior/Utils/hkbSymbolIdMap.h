/// @file
/// @brief hkbSymbolIdMap: translates a graph's internal symbol ids (events, variables) to and from external ids.
#pragma once

#include <Common/Base/hkBase.h>

namespace CB { namespace havok
{
    struct hkbSymbolIdMap : hkReferencedObject
    {
        hkArray<int>           m_internalToExternalMap;   ///< +0x10
        hkPointerMap<int, int> m_externalToInternalMap;   ///< +0x20, keyed external + 1

        HK_FORCE_INLINE int getExternal(int internal) const
        {
            if (internal < 0) return internal;
            return m_internalToExternalMap[internal];
        }
        HK_FORCE_INLINE int getInternal(int external) const
        {
            if (external < 0) return external;
            return static_cast<int>(m_externalToInternalMap.m_map.getWithDefault(external + 1, hkUlong(-1)));
        }

        /// @brief `id`, internal to `fromMap`'s graph, as the internal id of `toMap`'s (through the external id they
        ///        share); unchanged when both maps are the same, and a null map passes the id through.
        /// @note Name provisional. Out of line in vanilla (0x140ad6050).
        static int convertInternalId(const hkbSymbolIdMap* fromMap, const hkbSymbolIdMap* toMap, int id);

        /// @note Vanilla inlines it into the type registry's finish function; out of line here, it also brings the
        ///       class's vtable and scalar deleting destructor into Behavior/Utils/hkbSymbolIdMap.cpp.
        hkbSymbolIdMap(hkFinishLoadedObjectFlag flag);
        HK_FORCE_INLINE hkbSymbolIdMap() {}
    };
} }
