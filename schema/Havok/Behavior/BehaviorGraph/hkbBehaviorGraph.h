/// @file
/// @brief hkbBehaviorGraph and its shared data (event infos, string tables).
#pragma once

#include <Behavior/Event/hkbEvent.h>
#include <Behavior/Generator/hkbGenerator.h>
#include <Behavior/Utils/hkbSymbolIdMap.h>

namespace CB { namespace schema
{
    struct hkbBehaviorGraphStringData : hkReferencedObject
    {
        hkArray<hkStringPtr> m_eventNames;   ///< +0x10
    };

    struct hkbBehaviorGraphData : hkReferencedObject
    {
        char                                 m_pad10[0x40 - 0x10];
        hkArray<hkbEventInfo>                m_eventInfos;   ///< +0x40
        char                                 m_pad50[0x78 - 0x50];
        hkRefPtr<hkbBehaviorGraphStringData> m_stringData;   ///< +0x78

        /// @note An id past the table reads as no flags.
        HK_FORCE_INLINE bool isEventSilent(int id) const
        {
            const hkUint32 flags = (id < m_eventInfos.getSize()) ? m_eventInfos[id].m_flags : 0;
            return (flags & hkbEventInfo::FLAG_SILENT) != 0;
        }
    };

    class hkbBehaviorGraph : public hkbGenerator
    {
    public:
        char                           m_pad48[0x88 - 0x48];
        hkRefPtr<hkbBehaviorGraphData> m_data;         ///< +0x88
        char                           m_pad90[0xb8 - 0x90];
        hkRefPtr<hkbSymbolIdMap>       m_eventIDMap;   ///< +0xb8

        /// @brief The template node an event raised by `node` names as its sender.
        /// @note Name provisional.
        hkbNode* getNodeTemplate(const hkbNode* node);
    };
} }
