/// @file
/// @brief hkbBehaviorGraph and its shared data (event infos, string tables).
#pragma once

#include <Behavior/BehaviorGraph/hkbNodeInfo.h>
#include <Behavior/Event/hkbEvent.h>
#include <Behavior/Generator/hkbGenerator.h>
#include <Behavior/Utils/hkbSymbolIdMap.h>
#include <Common/Base/Algorithm/PseudoRandom/hkPseudoRandomGenerator.h>
#include <Common/Base/Container/PointerMap/hkPointerMap.h>

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
        /// @brief Each active node template's index in m_activeNodes.
        hkPointerMap<const hkbNode*, int>* m_activeNodeTemplateToIndexMap;   ///< +0xa0
        char                           m_padA8[0xb8 - 0xa8];
        hkRefPtr<hkbSymbolIdMap>       m_eventIDMap;   ///< +0xb8
        char                           m_padC0[0x12e - 0xc0];
        /// @brief Asks the graph to recompute its active nodes (a selector switched to another child).
        /// @note Name provisional: Havok's (the selector raises it under the monitor timer "MSG::requestUpdateActiveNodes").
        hkBool                         m_updateActiveNodes;   ///< +0x12e

        /// @brief The template node an event raised by `node` names as its sender.
        /// @note Name provisional.
        hkbNode* getNodeTemplate(const hkbNode* node);
        /// @brief This graph's clone of the template node `node`, else null.
        /// @note Name provisional (the inverse of getNodeTemplate).
        hkbNode* getNodeClone(const hkbNode* node);
        /// @brief Moves the active node `node` to `localTime` (its clone's setElapsedTime, slot 26) and carries the
        ///        move down to its synchronized active descendants, each placed at the same sync-point interval as its
        ///        parent.
        /// @note Name provisional; out of line (0x140ab3c10), called by the blender and the transition effects. `node`
        ///       keys m_activeNodeTemplateToIndexMap as is (not found: nothing moves).
        void setNodeLocalTime(const hkbNode* node, hkReal localTime);
        /// @brief As getNodeClone, but a template not cloned yet is cloned (cloneNode) and both maps learn the pair.
        /// @note Name provisional (0x140ab4020; BSCyclicBlendTransitionGenerator::activate calls it).
        hkbNode* getOrCreateNodeClone(hkbNode* node);
        /// @brief The active-node record of `node` (a template or this graph's clone of one), or the record it is a
        ///        proxy for.
        /// @pre `node` is active (an unknown node reads record 0).
        /// @note Name provisional. Inlined everywhere in vanilla (the transition effects, the selector's updateSync).
        HK_FORCE_INLINE hkbNodeInfo& getNodeInfo(const hkbNode* node)
        {
            if (node->m_cloneState == hkbNode::CLONE_STATE_CLONE) node = getNodeTemplate(node);
            hkbNodeInfo& info = (*m_activeNodes)[m_activeNodeTemplateToIndexMap->getWithDefault(node, 0)];
            return info.m_isProxy ? (*m_activeNodes)[info.m_proxyIndex] : info;
        }
        /// @brief Starts an echo of `echoDuration` seconds (hkbGenerator::startEcho) on every active generator from `node`
        ///        down; a zero duration restarts them.
        /// @note Name provisional. The transition effects' self transitions call it on the to-generator.
        void startEcho(hkReal echoDuration, hkbNode* node);
        /// @brief Pushes the graph's variable values into the bound node members of every active node.
        /// @note Name from its monitor timer ("hkbBehaviorGraph::copyVariablesToMembersRoot"); the flag is passed through
        ///       to each node's copy unchanged, meaning unknown (hkbSequence::update passes false).
        void copyVariablesToMembers(const hkbContext& context, bool flag);
    };
} }
