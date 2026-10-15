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
        /// @note An hkBool: hkbSequence::update saves it into a bool with `cmp byte,0; setnz` (a plain bool copies the byte).
        hkBool          m_directMap;     ///< +0x20 — name provisional: true = index the internal→external array

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
        /// @brief The id of a dequeued event, translated back (the inverse of toExternal).
        /// @note Name provisional. Reads the event's id in each branch, as vanilla does.
        HK_FORCE_INLINE int getInternalId(const hkbEventBase& e) const
        {
            if (!m_symbolIdMap) return e.m_id;
            if (!m_directMap) {
                const int id = e.m_id;
                return (id >= 0) ? m_symbolIdMap->m_internalToExternalMap.m_data[id] : id;
            }
            const int id = e.m_id;
            return (id >= 0) ? static_cast<int>(m_symbolIdMap->m_externalToInternalMap.getWithDefault(id + 1, hkUlong(-1))) : id;
        }
    };
} }
