/// @file
/// @brief hkbNodeInfo: a behavior graph's per-active-node record (the elements of hkbBehaviorGraph::m_activeNodes).
#pragma once

#include <Behavior/Generator/hkbGeneratorSyncInfo.h>

namespace CB { namespace havok
{
    class hkbNode;
    class hkbBehaviorGraph;

    /// @note 0x90 bytes. Only the fields the reimplemented code touches are named.
    struct hkbNodeInfo
    {
        hkbGeneratorSyncInfo m_syncInfo;         ///< +0x00
        hkbNode*             m_nodeTemplate;     ///< +0x50
        hkbNode*             m_nodeClone;        ///< +0x58
        hkbBehaviorGraph*    m_behavior;         ///< +0x60
        /// @note Name provisional: the index, in the ROOT graph's m_activeNodes, of the record that stands in for this one
        ///       while m_isProxy is set.
        hkInt32              m_proxyIndex;       ///< +0x68
        char                 m_pad6c[0x70 - 0x6c];
        /// @brief How many child indices this node has in the root graph's m_activeNodesChildrenIndices.
        /// @note Name provisional (a modifier's (de)activation asks for an active-node update when it is positive).
        hkInt32              m_numChildren;      ///< +0x70
        /// @brief Where this node's run of child indices starts in the root graph's m_activeNodesChildrenIndices.
        /// @note Name provisional (hkbBehaviorGraph::updateSync reads its first child through it).
        hkInt32              m_childrenIndicesStart;   ///< +0x74
        char                 m_pad78[0x84 - 0x78];
        /// @brief The node's events go nowhere: the graph update hands it a throwaway queue.
        /// @note Name provisional.
        hkBool               m_ignoreEvents;     ///< +0x84
        /// @brief The node is a generator (updated, then synced), not a modifier.
        /// @note Name provisional.
        hkBool               m_isGenerator;      ///< +0x85
        char                 m_pad86[0x88 - 0x86];
        /// @brief The node clone was activated (hkbBehaviorGraph::deactivate deactivates only these).
        /// @note Name provisional.
        hkBool               m_isActive;         ///< +0x88
        /// @note Name provisional: set when this record defers to m_activeNodes[m_proxyIndex] of the root graph (the clip's
        ///       updateSync and the blender's child lookups follow it).
        hkBool               m_isProxy;          ///< +0x89
        char                 m_pad8a[0x90 - 0x8a];
    };
    static_assert(sizeof(hkbNodeInfo) == 0x90, "hkbNodeInfo size");
} }
