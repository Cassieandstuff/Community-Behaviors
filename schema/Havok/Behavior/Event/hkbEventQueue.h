/// @file
/// @brief hkbEventQueue: the queue nodes raise events into, with the id remapping applied on the way in.
#pragma once

#include <Behavior/Event/hkbEvent.h>
#include <Behavior/Utils/hkbSymbolIdMap.h>

namespace CB { namespace schema
{
    struct hkbEventQueue : hkQueue<hkbEvent>
    {
        hkbSymbolIdMap* m_symbolIdMap;   ///< +0x18
        bool            m_directMap;     ///< +0x20 — name provisional: true = index the internal→external array

        /// @note Must stay force-inlined: under /Ob1 a plain `inline` is only a hint, and outlining it breaks callers.
        HK_FORCE_INLINE int toExternal(int id) const
        {
            if (m_symbolIdMap) {
                if (m_directMap) {
                    if (id >= 0) id = m_symbolIdMap->m_internalToExternalMap.m_data[id];
                } else {
                    if (id >= 0) id = static_cast<int>(m_symbolIdMap->m_externalToInternalMap.getWithDefault(id + 1, hkUlong(-1)));
                }
            }
            return id;
        }
    };
} }
