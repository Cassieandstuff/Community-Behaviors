/// @file
/// @brief hkbBehaviorGraph and its shared data (event infos, string tables).
#pragma once

#include <Behavior/BehaviorGraph/hkbNodeInfo.h>
#include <Behavior/Event/hkbEvent.h>
#include <Behavior/Generator/hkbGenerator.h>
#include <Behavior/Utils/hkbSymbolIdMap.h>
#include <Common/Base/Algorithm/PseudoRandom/hkPseudoRandomGenerator.h>

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

        /// @note An id past the table reads as no flags.
        HK_FORCE_INLINE bool isEventSyncPoint(int id) const
        {
            const hkUint32 flags = (id < m_eventInfos.getSize()) ? m_eventInfos[id].m_flags : 0;
            return (flags & hkbEventInfo::FLAG_SYNC_POINT) != 0;
        }
    };

    class hkbBehaviorGraph : public hkbGenerator
    {
    public:
        char                           m_pad48[0x78 - 0x48];
        hkPseudoRandomGenerator*       m_pseudoRandomGenerator;   ///< +0x78 — reflected serialize-ignored
        char                           m_pad80[0x88 - 0x80];
        hkRefPtr<hkbBehaviorGraphData> m_data;         ///< +0x88
        char                           m_pad90[0x98 - 0x90];
        hkArray<hkbNodeInfo>*          m_activeNodes;  ///< +0x98
        char                           m_padA0[0xb8 - 0xa0];
        hkRefPtr<hkbSymbolIdMap>       m_eventIDMap;   ///< +0xb8

        /// @brief The template node an event raised by `node` names as its sender.
        /// @note Name provisional.
        hkbNode* getNodeTemplate(const hkbNode* node);
        /// @brief Pushes the graph's variable values into the bound node members of every active node.
        /// @note Name from its monitor timer ("hkbBehaviorGraph::copyVariablesToMembersRoot"); the flag is passed through
        ///       to each node's copy unchanged, meaning unknown (hkbSequence::update passes false).
        void copyVariablesToMembers(const hkbContext& context, bool flag);
    };
} }
