/// @file
/// @brief hkbBehaviorGraph, reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/BehaviorGraph/hkbBehaviorGraph.h>

#include <Behavior/Context/hkbContext.h>
#include <Behavior/Event/hkbEventQueue.h>
#include <Behavior/Generator/hkbGeneratorOutputUtils.h>

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
        m_globalTransitionData(HK_NULL),
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
        m_globalTransitionData(HK_NULL),
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

    const hkClass& hkbBehaviorGraph::staticClass()
    {
        return hkbBehaviorGraphClass;
    }

    const void* hkbBehaviorGraph::getClassType() const
    {
        return &staticClass();
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
