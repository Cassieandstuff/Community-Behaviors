/// @file
/// @brief hkbNodeInfo: a behavior graph's per-active-node record (the elements of hkbBehaviorGraph::m_activeNodes).
#pragma once

#include <Behavior/Generator/hkbGeneratorSyncInfo.h>

namespace CB { namespace schema
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
        char                 m_pad6c[0x89 - 0x6c];
        /// @note Name provisional: set when this record defers to m_activeNodes[m_proxyIndex] of the root graph (the clip's
        ///       updateSync and the blender's child lookups follow it).
        hkBool               m_isProxy;          ///< +0x89
        char                 m_pad8a[0x90 - 0x8a];
    };
    static_assert(sizeof(hkbNodeInfo) == 0x90, "hkbNodeInfo size");
} }
