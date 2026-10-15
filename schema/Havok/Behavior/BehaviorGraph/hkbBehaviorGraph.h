/// @file
/// @brief hkbBehaviorGraph and its shared data (event infos, string tables).
#pragma once

#include <Behavior/BehaviorGraph/hkbNodeInfo.h>
#include <Behavior/Event/hkbEvent.h>
#include <Behavior/Generator/hkbGenerator.h>
#include <Behavior/Utils/hkbSymbolIdMap.h>
#include <Behavior/Variables/hkbVariableInfo.h>
#include <Behavior/Variables/hkbVariableValueSet.h>
#include <Common/Base/Algorithm/PseudoRandom/hkPseudoRandomGenerator.h>
#include <Common/Base/Container/PointerMap/hkPointerMap.h>

namespace CB { namespace schema
{
    extern const hkClass hkbBehaviorGraphClass;
    class hkbStateListener;

    struct hkbBehaviorGraphStringData : hkReferencedObject
    {
        hkArray<hkStringPtr> m_eventNames;               ///< +0x10
        hkArray<hkStringPtr> m_attributeNames;           ///< +0x20
        hkArray<hkStringPtr> m_variableNames;            ///< +0x30
        hkArray<hkStringPtr> m_characterPropertyNames;   ///< +0x40
    };

    struct hkbBehaviorGraphData : hkReferencedObject
    {
        hkArray<hkReal>                      m_attributeDefaults;        ///< +0x10
        hkArray<hkbVariableInfo>             m_variableInfos;            ///< +0x20
        hkArray<hkbVariableInfo>             m_characterPropertyInfos;   ///< +0x30
        hkArray<hkbEventInfo>                m_eventInfos;   ///< +0x40
        /// @brief Per-variable bounds (empty when no variable has any).
        hkArray<hkbVariableValue>            m_wordMinVariableValues;    ///< +0x50
        hkArray<hkbVariableValue>            m_wordMaxVariableValues;    ///< +0x60
        hkRefPtr<hkbVariableValueSet>        m_variableInitialValues;    ///< +0x70
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

    /// @brief A graph's saved state: one record per node that has internal state, plus the variable values.
    /// @brief One node's saved state: its sync info, name, internal state object and id.
    /// @note Layout per the Havok SDK's hkbNodeInternalStateInfo; only m_nodeId is read so far.
    struct hkbNodeInternalStateInfo : hkReferencedObject
    {
        hkbGeneratorSyncInfo         m_syncInfo;                 ///< +0x10
        hkStringPtr                  m_name;                    ///< +0x60
        hkRefPtr<hkReferencedObject> m_internalState;           ///< +0x68
        hkInt16                      m_nodeId;                  ///< +0x70
        hkBool                       m_hasActivateBeenCalled;   ///< +0x72
    };

    struct hkbBehaviorGraphInternalState : hkReferencedObject
    {
        hkArray<hkbNodeInternalStateInfo*> m_nodeInternalStateInfos;   ///< +0x10
        hkbVariableValueSet*            m_variableValueSet;         ///< +0x20
    };

    /// @note Only referenced so far.
    struct hkbGlobalTransitionData : hkReferencedObject {};

    /// @note The member names after +0x88 are the Havok SDK's reflected (serialize-ignored) runtime members, matched to
    ///       the schema's 14 ignored pointers by position.
    class hkbBehaviorGraph : public hkbGenerator
    {
    public:
        /// @note Values per the Havok SDK.
        enum VariableMode { VARIABLE_MODE_DISCARD_WHEN_INACTIVE = 0, VARIABLE_MODE_MAINTAIN_VALUES_WHEN_INACTIVE = 1 };

        hkInt8                         m_variableMode;            ///< +0x48
        hkArray<hkInt16>               m_uniqueIdPool;            ///< +0x50
        hkPointerMap<int, const hkbNode*>* m_idToStateMachineTemplateMap;   ///< +0x60 — state machine id → template (values per the SDK)
        hkArray<int>                   m_mirroredExternalIdMap;   ///< +0x68
        hkPseudoRandomGenerator*       m_pseudoRandomGenerator;   ///< +0x78 — reflected serialize-ignored
        hkRefPtr<hkbGenerator>         m_rootGenerator;           ///< +0x80
        hkRefPtr<hkbBehaviorGraphData> m_data;         ///< +0x88
        hkbGenerator*                  m_rootGeneratorClone;      ///< +0x90
        hkArray<hkbNodeInfo>*          m_activeNodes;  ///< +0x98
        /// @brief Each active node template's index in m_activeNodes.
        hkPointerMap<const hkbNode*, int>* m_activeNodeTemplateToIndexMap;   ///< +0xa0
        /// @brief The active nodes' child indices (into m_activeNodes), each node's run starting at its
        ///        hkbNodeInfo::m_childrenIndicesStart.
        hkArray<int>*                  m_activeNodesChildrenIndices;   ///< +0xa8
        hkRefPtr<hkbGlobalTransitionData> m_globalTransitionData; ///< +0xb0
        hkRefPtr<hkbSymbolIdMap>       m_eventIDMap;   ///< +0xb8
        hkRefPtr<hkbSymbolIdMap>       m_attributeIdMap;          ///< +0xc0
        hkRefPtr<hkbSymbolIdMap>       m_variableIdMap;           ///< +0xc8
        hkRefPtr<hkbSymbolIdMap>       m_characterPropertyIdMap;  ///< +0xd0
        hkbVariableValueSet*           m_variableValueSet;        ///< +0xd8
        hkPointerMap<const hkbNode*, hkbNode*>* m_nodeTemplateToCloneMap;   ///< +0xe0
        hkPointerMap<const hkbNode*, hkbNode*>* m_nodeCloneToTemplateMap;   ///< +0xe8
        /// @brief Each state listener template's clone (the clones referenced by the map).
        hkPointerMap<const hkReferencedObject*, hkReferencedObject*>* m_stateListenerTemplateToCloneMap;  ///< +0xf0
        void*                          m_nodePartitionInfo;       ///< +0xf8
        int                            m_numIntermediateOutputs;  ///< +0x100
        hkArray<hkbNode*>              m_jobs;                    ///< +0x108
        hkArray<hkbNode*>              m_allPartitionMemory;      ///< +0x118
        hkInt16                        m_numStaticNodes;          ///< +0x128
        hkInt16                        m_nextUniqueId;            ///< +0x12a
        hkBool                         m_isActive;                ///< +0x12c
        hkBool                         m_isLinked;                ///< +0x12d
        /// @brief Asks the graph to recompute its active nodes (a selector switched to another child).
        /// @note Name provisional: Havok's (the selector raises it under the monitor timer "MSG::requestUpdateActiveNodes").
        hkBool                         m_updateActiveNodes;   ///< +0x12e
        hkBool                         m_stateOrTransitionChanged;   ///< +0x12f

        /// @note Allocates the pseudo-random generator (seed 1321) up front, as both of vanilla's constructors do.
        hkbBehaviorGraph();
        /// @brief A graph over `rootGenerator` and `data` (both referenced).
        hkbBehaviorGraph(hkbGenerator* rootGenerator, hkbBehaviorGraphData* data);
        /// @note The id maps, data and transition data release through their setters; a graph that cloned its nodes destroys
        ///       the clones in place and frees their block.
        virtual ~hkbBehaviorGraph();                                                                     ///< slot 0

        /// @note Out of line among the type-registration getters (0x140ac2a20); getClassType tail-jumps to it.
        static const hkClass& staticClass();

        virtual const void* getClassType() const;                                                      ///< slot 1
        virtual void        calcContentStatistics(hkStatisticsCollector* collector, const hkClass* cls) const;   ///< slot 2
        /// @brief Builds the active-node tables and activates the active nodes — only as a root (no behavior in the
        ///        context yet).
        virtual void        activate(const hkbContext& context);                                       ///< slot 4
        /// @brief Only as a root: processes the event (handleEventInternal).
        /// @brief Only as a root: the per-frame update of every active node (each in its own graph's context and event map,
        ///        with its own timestep), then either the generators' sync pass or, when a node asked for it, an active-node
        ///        update.
        virtual void        update(const hkbContext& context, hkReal timestep);                       ///< slot 5
        virtual void        handleEvent(const hkbContext& context, const hkbEvent e);                  ///< slot 6
        /// @brief Only as a root: deactivates every active node (each in its own graph's context), releases the clones
        ///        and the active-node tables, and drops the variable values unless they are kept while inactive.
        virtual void        deactivate(const hkbContext& context);                                     ///< slot 7
        virtual void        getChildren(GetChildrenFlags flags, ChildrenInfo& childrenInfo);           ///< slot 9
        /// @brief Shared, never copied: a behavior graph is its own clone.
        virtual hkbNode*    cloneNode(hkbBehaviorGraph& rootBehavior) const;                           ///< slot 12
        /// @note hkbNode's returns false.
        virtual bool        vfn22();                                                                   ///< slot 22
        /// @brief Passes the root generator's output through.
        virtual void        generate(const hkbContext& context, const hkbGeneratorOutput** activeChildrenOutput, hkbGeneratorOutput& output,
                                     hkReal timeOffset);                                               ///< slot 23
        virtual bool        canRecycleOutput() const;                                                  ///< slot 24
        /// @brief Defers this graph's sync info to its root generator's record.
        virtual void        updateSync(const hkbContext& context, hkbNodeInfo& info);                   ///< slot 25

        /// @brief Activates the graph from a saved internal state (node states keyed by node id, variable values).
        /// @note Name provisional (the Havok SDK's); pinned as hkbBehaviorGraph_CollectUsedBindingIndices before.
        void activateWithInternalState(const hkbContext& context, const hkbBehaviorGraphInternalState& internalState);
        /// @brief Returns a node's unique id to the pool (ids below m_numStaticNodes stay with the template).
        /// @note Name provisional.
        void releaseUniqueId(hkInt16 id);
        /// @brief A unique id for a node cloned at runtime (from the pool, else the next one).
        /// @note Name provisional. Out of line (0x140ab4700); the state machine ids the transition effects it clones.
        hkInt16 getUniqueId();
        /// @brief Creates the variable value set from the graph data's initial values, if the data declares variables
        ///        and there is no set yet.
        /// @note Name provisional; out of line in vanilla (0x140ab32b0).
        void initVariableValueSet();
        /// @brief The root's event pass: offers the event to every active node in its own graph's context.
        /// @note Name provisional; out of line in vanilla (0x140ab58a0).
        void handleEventInternal(const hkbContext& context, const hkbEvent e);
        /// @brief Releases the node clones and frees the active-node tables.
        /// @note Name provisional.
        void deleteActiveNodes();
        /// @brief Each active node's timestep for this frame (from the last record back; synced children adjusted).
        /// @note Name provisional (its monitor split is "ComputeTimesteps"); out of line in vanilla (0x140ab6ad0).
        void computeTimesteps(hkReal timestep, hkReal* timestepsOut);
        /// @note Setters (each references the new value, releases the old).
        void setData(hkbBehaviorGraphData* data);
        void setEventIdMap(hkbSymbolIdMap* eventIdMap);
        void setAttributeIdMap(hkbSymbolIdMap* attributeIdMap);
        void setVariableIdMap(hkbSymbolIdMap* variableIdMap);
        void setCharacterPropertyIdMap(hkbSymbolIdMap* characterPropertyIdMap);
        /// @note Name provisional (the member at +0xb0).
        void setGlobalTransitionData(hkbGlobalTransitionData* globalTransitionData);
        /// @brief Frees the partition and job blocks (from the thread's temp allocator).
        /// @note Name provisional; out of line in vanilla (0x140ab7290).
        void deletePartitionMemory();

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
        /// @brief This graph's clone of the state listener `listener` (made with hkbStateListener::clone the first time and
        ///        remembered), with one more reference; null when the listener does not clone.
        /// @note Name provisional. Out of line (0x140ab40c0); hkbStateMachine's clone copy calls it.
        hkbStateListener* getStateListenerClone(hkbStateListener* listener);
        /// @brief The active-node record of `node` (a template or this graph's clone of one), or the record it is a
        ///        proxy for.
        /// @pre `node` is active (an unknown node reads record 0).
        /// @note Name provisional. Inlined everywhere in vanilla (the transition effects, the selector's updateSync).
        HK_FORCE_INLINE hkbNodeInfo& getNodeInfo(const hkbNode* node)
        {
            if (node->m_cloneState == hkbNode::CLONE_STATE_CLONE) node = getNodeTemplate(node);
            hkbNodeInfo& info = (*m_activeNodes)[m_activeNodeTemplateToIndexMap->getWithDefault(node, 0)];
            if (!info.m_isProxy) return info;
            return (*m_activeNodes)[info.m_proxyIndex];
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

    /// @brief The graph's heavy half, compiled apart from hkbBehaviorGraph.cpp.
    struct hkbBehaviorGraphUtils
    {
        /// @brief Recomputes the graph's active nodes, activating new ones (restored from `nodeIdToInternalState` when
        ///        given) and deactivating dropped ones.
        /// @note Name and parameter type provisional; out of line in vanilla (0x140b0fb70). activateWithInternalState passes
        ///       a map of the saved records keyed by node id; activate and the event pass pass null.
        static void updateActiveNodes(hkbBehaviorGraph& graph, const hkbContext& context,
                                      const hkPointerMap<int, const hkbNodeInternalStateInfo*>* nodeIdToInternalState);
        /// @brief Collects the nodes under `root` (as node-info records, the node in m_nodeTemplate).
        /// @note Name, flag and the last (pointer-sized) parameter provisional; out of line in vanilla (0x140ad5350, beside the
        ///       modifier steps).
        ///       The graph destructor passes flags 4 and 0.
        static void collectNodes(hkbNode* root, hkbBehaviorGraph* rootBehavior, int flags, hkArray<hkbNodeInfo>& nodesOut, void* unknown);
        /// @brief Frees the single block the node clones of a behavior were allocated in (each clone's destructor has run).
        /// @note Name provisional; out of line in vanilla (0x140b13ca0).
        static hkResult freeCloneBlock(void* block);
    };
} }
