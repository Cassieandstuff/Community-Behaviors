/// @file
/// @brief hkbBehaviorGraph, reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/BehaviorGraph/hkbBehaviorGraph.h>

#include <Behavior/Context/hkbContext.h>
#include <Behavior/Event/hkbEventQueue.h>
#include <Behavior/Generator/hkbGeneratorOutputUtils.h>
#include <Behavior/Modifier/hkbModifier.h>
#include <Common/Base/Memory/hkMemory.h>
#include <Common/Base/Memory/Util/Stats/hkStatisticsCollector.h>

namespace CB { namespace schema
{
    hkbBehaviorGraph::hkbBehaviorGraph()
    :   m_variableMode(VARIABLE_MODE_DISCARD_WHEN_INACTIVE),
        m_idToStateMachineTemplateMap(HK_NULL),
        m_pseudoRandomGenerator(HK_NULL),
        m_rootGeneratorClone(HK_NULL),
        m_activeNodes(HK_NULL),
        m_activeNodeTemplateToIndexMap(HK_NULL),
        m_activeNodesChildrenIndices(HK_NULL),
        m_variableValueSet(HK_NULL),
        m_nodeTemplateToCloneMap(HK_NULL),
        m_nodeCloneToTemplateMap(HK_NULL),
        m_stateListenerTemplateToCloneMap(HK_NULL),
        m_nodePartitionInfo(HK_NULL),
        m_numIntermediateOutputs(0),
        m_numStaticNodes(-1),
        m_nextUniqueId(-1),
        m_isActive(false),
        m_isLinked(false),
        m_updateActiveNodes(false),
        m_stateOrTransitionChanged(false)
    {
        m_pseudoRandomGenerator = new hkPseudoRandomGenerator(1321);
    }

    hkbBehaviorGraph::hkbBehaviorGraph(hkbGenerator* rootGenerator, hkbBehaviorGraphData* data)
    :   m_variableMode(VARIABLE_MODE_DISCARD_WHEN_INACTIVE),
        m_idToStateMachineTemplateMap(HK_NULL),
        m_pseudoRandomGenerator(HK_NULL),
        m_rootGenerator(rootGenerator),
        m_data(data),
        m_rootGeneratorClone(HK_NULL),
        m_activeNodes(HK_NULL),
        m_activeNodeTemplateToIndexMap(HK_NULL),
        m_activeNodesChildrenIndices(HK_NULL),
        m_variableValueSet(HK_NULL),
        m_nodeTemplateToCloneMap(HK_NULL),
        m_nodeCloneToTemplateMap(HK_NULL),
        m_stateListenerTemplateToCloneMap(HK_NULL),
        m_nodePartitionInfo(HK_NULL),
        m_numIntermediateOutputs(0),
        m_numStaticNodes(-1),
        m_nextUniqueId(-1),
        m_isActive(false),
        m_isLinked(false),
        m_updateActiveNodes(false),
        m_stateOrTransitionChanged(false)
    {
        m_pseudoRandomGenerator = new hkPseudoRandomGenerator(1321);
    }

    /// @note A root that never cloned its nodes (no template-to-clone map) owns the clones its active nodes resolve to;
    ///       a graph that cloned its root generator destroys every collected clone in place, then frees their block.
    hkbBehaviorGraph::~hkbBehaviorGraph()
    {
        if (m_activeNodes && m_nodeTemplateToCloneMap == HK_NULL) {
            const int numActive = m_activeNodes->getSize();
            for (int i = 0; i < numActive; i++) {
                hkbNode* clone = getNodeClone((*m_activeNodes)[i].m_nodeTemplate);
                if (clone->m_cloneState == CLONE_STATE_CLONE || clone->m_cloneState == CLONE_STATE_SHARABLE) clone->removeReference();
            }
        }
        if (m_rootGenerator) {
            m_rootGenerator = HK_NULL;
            if (m_rootGeneratorClone) {
                {
                    hkArray<hkbNodeInfo> nodes;
                    hkbBehaviorGraphUtils::collectNodes(m_rootGeneratorClone, this, 4, nodes, HK_NULL);
                    const int numNodes = nodes.getSize();
                    for (int i = 0; i < numNodes; i++) nodes[i].m_nodeTemplate->~hkbNode();
                }
                hkbBehaviorGraphUtils::freeCloneBlock(m_rootGeneratorClone);
            }
        }
        if (m_pseudoRandomGenerator) delete m_pseudoRandomGenerator;
        setEventIdMap(HK_NULL);
        setAttributeIdMap(HK_NULL);
        setVariableIdMap(HK_NULL);
        setCharacterPropertyIdMap(HK_NULL);
        setData(HK_NULL);
        setGlobalTransitionData(HK_NULL);
        deleteActiveNodes();
        if (m_variableValueSet) m_variableValueSet->removeReference();
        if (m_nodeTemplateToCloneMap == HK_NULL) delete m_idToStateMachineTemplateMap;
        m_idToStateMachineTemplateMap = HK_NULL;
        delete m_nodeTemplateToCloneMap;
        delete m_nodeCloneToTemplateMap;
        if (m_stateListenerTemplateToCloneMap) {
            for (hkPointerMap<const hkReferencedObject*, hkReferencedObject*>::Iterator it = m_stateListenerTemplateToCloneMap->getIterator();
                 m_stateListenerTemplateToCloneMap->isValid(it); it = m_stateListenerTemplateToCloneMap->getNext(it)) {
                m_stateListenerTemplateToCloneMap->getValue(it)->removeReference();
            }
            delete m_stateListenerTemplateToCloneMap;
        }
        deletePartitionMemory();
    }

    void hkbBehaviorGraph::setData(hkbBehaviorGraphData* data)
    {
        m_data = data;
    }

    void hkbBehaviorGraph::setEventIdMap(hkbSymbolIdMap* eventIdMap)
    {
        m_eventIDMap = eventIdMap;
    }

    void hkbBehaviorGraph::setAttributeIdMap(hkbSymbolIdMap* attributeIdMap)
    {
        m_attributeIdMap = attributeIdMap;
    }

    void hkbBehaviorGraph::setVariableIdMap(hkbSymbolIdMap* variableIdMap)
    {
        m_variableIdMap = variableIdMap;
    }

    void hkbBehaviorGraph::setCharacterPropertyIdMap(hkbSymbolIdMap* characterPropertyIdMap)
    {
        m_characterPropertyIdMap = characterPropertyIdMap;
    }

    void hkbBehaviorGraph::setGlobalTransitionData(hkbGlobalTransitionData* globalTransitionData)
    {
        m_globalTransitionData = globalTransitionData;
    }

    const hkClass& hkbBehaviorGraph::staticClass()
    {
        return hkbBehaviorGraphClass;
    }

    const void* hkbBehaviorGraph::getClassType() const
    {
        return &staticClass();
    }

    /// @note The per-record chunks are addressed as `&m_activeNodes[i]` on the array POINTER (16-byte strides through
    ///       the hkArray headers, sized as records), as vanilla does; m_characterPropertyIdMap is not reported.
    void hkbBehaviorGraph::calcContentStatistics(hkStatisticsCollector* collector, const hkClass* cls) const
    {
        hkStatisticsCollectorUtil::addDeclaredMembers(this, &hkbBehaviorGraphClass, collector);
        if (m_activeNodes) {
            collector->addArray("activeNodes", *m_activeNodes, hkStatisticsCollector::MEMORY_RUNTIME);
            for (int i = 0; i < m_activeNodes->getSize(); i++) {
                collector->addNormalChunk("activeNodes", &m_activeNodes[i], sizeof(hkbNodeInfo), 0);
            }
            const hkPointerMapStorage& map  = m_activeNodeTemplateToIndexMap->m_map;
            const int                  size = map.getMemSize();
            collector->addChunk(hkStatisticsCollector::MEMORY_NORMAL, "activeNodeTemplateToIndexMap", map.m_elem, size,
                                hkStatisticsCollector::getAllocatedSize(map.m_elem, size));
            collector->addArray("activeNodesChildrenIndices", *m_activeNodesChildrenIndices, hkStatisticsCollector::MEMORY_RUNTIME);
        }
        collector->addReferencedObject("eventIdMap", m_eventIDMap);
        collector->addReferencedObject("attributeIdMap", m_attributeIdMap);
        collector->addReferencedObject("variableIdMap", m_variableIdMap);
        if (m_variableValueSet) collector->addReferencedObject("variableValueSet", m_variableValueSet);
        hkbGenerator::calcContentStatistics(collector, &hkbGeneratorClass);
    }

    /// @note Writes the context's behavior back to null through the const reference (the caller's context is reused).
    void hkbBehaviorGraph::activate(const hkbContext& context)
    {
        if (context.m_behavior == HK_NULL) {
            if (m_data) initVariableValueSet();
            m_activeNodes                  = new hkArray<hkbNodeInfo>;
            m_activeNodeTemplateToIndexMap = new hkPointerMap<const hkbNode*, int>;
            m_activeNodesChildrenIndices   = new hkArray<int>;
            m_isActive                     = true;
            hkbBehaviorGraphUtils::updateActiveNodes(*this, context, HK_NULL);
            const_cast<hkbContext&>(context).m_behavior = HK_NULL;
        }
    }

    /// @note Monitor timers as vanilla's (list "hkbBehaviorGraph::update", one "SingleNodeUpdate" list per node). Nodes
    ///       whose m_userData has bit 0 set are skipped (a Bethesda flag; meaning not identified).
    void hkbBehaviorGraph::update(const hkbContext& context, hkReal timestep)
    {
        if (context.m_behavior == HK_NULL) {
            hkbContext&    ctx   = const_cast<hkbContext&>(context);
            hkbEventQueue* queue = context.getEventQueue();
            HK_TIMER_BEGIN_LIST("hkbBehaviorGraph::update", "CopyVariablesToMembers");
            HK_TIMER_SPLIT_LIST("UpdateActiveNodes");
            if (m_updateActiveNodes) hkbBehaviorGraphUtils::updateActiveNodes(*this, context, HK_NULL);
            const int numActiveNodes = m_activeNodes->getSize();
            HK_TIMER_SPLIT_LIST("ComputeTimesteps");
            hkReal* timesteps = hkAllocateStack<hkReal>(numActiveNodes);
            computeTimesteps(timestep, timesteps);
            HK_TIMER_SPLIT_LIST("UpdateNodes");
            hkbSymbolIdMap* oldMap    = queue->m_symbolIdMap;
            const bool      oldDirect = queue->m_directMap;
            hkMath::prefetch128((*m_activeNodes)[0].m_nodeClone);
            for (int i = 0; i < numActiveNodes; i++) {
                HK_TIMER_BEGIN_LIST("SingleNodeUpdate", "GetCloneAndSetupEventMap");
                if (i < numActiveNodes - 1) hkMath::prefetch128((*m_activeNodes)[i + 1].m_nodeClone);
                const hkReal   nodeTimestep = timesteps[i];
                hkbNodeInfo&   info         = (*m_activeNodes)[i];
                hkbNode*       node         = info.m_nodeClone;
                ctx.m_behavior              = info.m_behavior;
                hkbEventQueue* contextQueue = context.m_eventQueue;
                hkbEventQueue  ignoredEvents;
                if (info.m_ignoreEvents) {
                    ctx.m_eventQueue = &ignoredEvents;
                } else {
                    queue->m_symbolIdMap = info.m_behavior->m_eventIDMap;
                    queue->m_directMap   = true;
                }
                if (!(node->m_userData & 1)) {
                    if (info.m_isGenerator) {
                        HK_TIMER_SPLIT_LIST("UpdateSingleGenerator");
                        HK_TIMER_BEGIN_LIST("SingleGeneratorUpdate", "CopyVariablesToNodes");
                        const int hasBindings = node->copyVariablesToMembers(context, false);
                        HK_TIMER_SPLIT_LIST("UpdateGeneratorNode");
                        node->update(context, nodeTimestep);
                        HK_TIMER_SPLIT_LIST("CopyVariablesFromNodes");
                        if (hasBindings) node->copyVariablesToMembers(context, true);
                        HK_TIMER_END_LIST();
                    } else {
                        HK_TIMER_SPLIT_LIST("UpdateSingleModifier");
                        hkbModifier::updateModifier(context, static_cast<hkbModifier*>(node), nodeTimestep, info);
                    }
                }
                HK_TIMER_END_LIST();
                ctx.m_eventQueue = contextQueue;
            }
            queue->m_symbolIdMap = oldMap;
            queue->m_directMap   = oldDirect;
            if (m_updateActiveNodes) {
                HK_TIMER_SPLIT_LIST("ActiveNodeUpdate");
                hkbBehaviorGraphUtils::updateActiveNodes(*this, context, HK_NULL);
            } else {
                HK_TIMER_SPLIT_LIST("UpdateSync");
                for (int i = 0; i < m_activeNodes->getSize(); i++) {
                    hkbNodeInfo& info = (*m_activeNodes)[i];
                    if (info.m_isGenerator) {
                        hkbGenerator* generator = static_cast<hkbGenerator*>(info.m_nodeClone);
                        ctx.m_behavior          = info.m_behavior;
                        generator->updateSync(context, info);
                    }
                }
            }
            ctx.m_behavior = HK_NULL;
            HK_TIMER_END_LIST();
            hkDeallocateStack(timesteps, numActiveNodes);
        }
    }

    void hkbBehaviorGraph::handleEvent(const hkbContext& context, const hkbEvent e)
    {
        if (context.m_behavior == HK_NULL) handleEventInternal(context, e);
    }

    void hkbBehaviorGraph::deactivate(const hkbContext& context)
    {
        if (context.m_behavior == HK_NULL) {
            hkbContext&     ctx       = const_cast<hkbContext&>(context);
            hkbEventQueue*  queue     = context.getEventQueue();
            hkbSymbolIdMap* oldMap    = queue->m_symbolIdMap;
            const bool      oldDirect = queue->m_directMap;
            const int       numActive = m_activeNodes->getSize();
            for (int i = 0; i < numActive; i++) {
                hkbNode* node = (*m_activeNodes)[i].m_nodeClone;
                if ((*m_activeNodes)[i].m_isActive) {
                    ctx.m_behavior       = (*m_activeNodes)[i].m_behavior;
                    queue->m_symbolIdMap = ctx.m_behavior->m_eventIDMap;
                    queue->m_directMap   = true;
                    node->deactivate(context);
                    releaseUniqueId(node->m_id);
                }
                if (m_nodeTemplateToCloneMap == HK_NULL && (node->m_cloneState == CLONE_STATE_CLONE || node->m_cloneState == CLONE_STATE_SHARABLE)) {
                    node->removeReference();
                }
            }
            deleteActiveNodes();
            if (m_variableMode == VARIABLE_MODE_DISCARD_WHEN_INACTIVE && m_variableValueSet) {
                m_variableValueSet->removeReference();
                m_variableValueSet = HK_NULL;
            }
            queue->m_symbolIdMap = oldMap;
            queue->m_directMap   = oldDirect;
            ctx.m_behavior       = HK_NULL;
            m_isActive           = false;
        }
    }

    void hkbBehaviorGraph::getChildren(GetChildrenFlags flags, ChildrenInfo& childrenInfo)
    {
        childrenInfo.m_childInfos.setSize(1);
        childrenInfo.m_childInfos[0].m_node = m_rootGenerator;
        if (flags & FLAG_ACTIVE_ONLY) {
            childrenInfo.m_childInfos[0].m_isActive = true;
            childrenInfo.m_anyActive                = true;
        }
    }

    hkbNode* hkbBehaviorGraph::cloneNode(hkbBehaviorGraph& rootBehavior) const
    {
        hkbBehaviorGraph* self = const_cast<hkbBehaviorGraph*>(this);
        self->m_cloneState     = CLONE_STATE_SHARABLE;
        addReference();
        return self;
    }

    void hkbBehaviorGraph::generate(const hkbContext& context, const hkbGeneratorOutput** activeChildrenOutput, hkbGeneratorOutput& output,
                                    hkReal timeOffset)
    {
        const hkbGeneratorOutput* rootOutput = activeChildrenOutput[0];
        if (&output != rootOutput) hkbGeneratorOutputUtils::copyGeneratorOutput(*rootOutput, output);
    }

    bool hkbBehaviorGraph::canRecycleOutput() const
    {
        return true;
    }

    void hkbBehaviorGraph::updateSync(const hkbContext& context, hkbNodeInfo& info)
    {
        hkbBehaviorGraph* rootBehavior = context.m_character->m_behaviorGraph;
        const int         childIndex   = (*rootBehavior->m_activeNodesChildrenIndices)[info.m_childrenIndicesStart];
        info.m_isProxy                 = true;
        info.m_proxyIndex =
            (*rootBehavior->m_activeNodes)[childIndex].m_isProxy ? (*rootBehavior->m_activeNodes)[childIndex].m_proxyIndex : childIndex;
    }

    void hkbBehaviorGraph::releaseUniqueId(hkInt16 id)
    {
        if (id >= 0 && id >= m_numStaticNodes) m_uniqueIdPool.pushBack(id);
    }


    /// @note The saved records are keyed by node id in a map over stack storage; it is emptied before the storage goes
    ///       back, and again by its destructor.
    void hkbBehaviorGraph::activateWithInternalState(const hkbContext& context, const hkbBehaviorGraphInternalState& internalState)
    {
        const int numInfos = internalState.m_nodeInternalStateInfos.getSize();
        const int mapSize  = hkPointerMap<int, const hkbNodeInternalStateInfo*>::getSizeInBytesFor(numInfos);
        char*     storage  = hkAllocateStack<char>(mapSize);
        hkPointerMap<int, const hkbNodeInternalStateInfo*> nodeIdToInternalState(storage, mapSize);
        for (int i = 0; i < numInfos; i++) {
            const hkbNodeInternalStateInfo* info = internalState.m_nodeInternalStateInfos[i];
            nodeIdToInternalState.insert(info->m_nodeId, info);
        }

        if (m_data && m_data->m_variableInfos.getSize() > 0) {
            if (m_variableValueSet == HK_NULL) m_variableValueSet = new hkbVariableValueSet;
            *m_variableValueSet = *internalState.m_variableValueSet;
        }

        m_activeNodes                  = new hkArray<hkbNodeInfo>;
        m_activeNodeTemplateToIndexMap = new hkPointerMap<const hkbNode*, int>;
        m_activeNodesChildrenIndices   = new hkArray<int>;
        m_isActive                     = true;
        hkbBehaviorGraphUtils::updateActiveNodes(*this, context, &nodeIdToInternalState);
        const_cast<hkbContext&>(context).m_behavior = HK_NULL;

        nodeIdToInternalState.clearAndDeallocate();
        hkDeallocateStack(storage, mapSize);
    }

    void hkbBehaviorGraph::deleteActiveNodes()
    {
        if (m_nodeTemplateToCloneMap) {
            for (hkPointerMap<const hkbNode*, hkbNode*>::Iterator it = m_nodeTemplateToCloneMap->getIterator(); m_nodeTemplateToCloneMap->isValid(it); it = m_nodeTemplateToCloneMap->getNext(it)) {
                m_nodeTemplateToCloneMap->getValue(it)->removeReference();
            }
            m_nodeTemplateToCloneMap->clear();
            m_nodeCloneToTemplateMap->clear();
        }
        delete m_activeNodes;
        m_activeNodes = HK_NULL;
        delete m_activeNodeTemplateToIndexMap;
        m_activeNodeTemplateToIndexMap = HK_NULL;
        delete m_activeNodesChildrenIndices;
        m_activeNodesChildrenIndices = HK_NULL;
    }

    bool hkbBehaviorGraph::vfn22()
    {
        return true;
    }
} }
