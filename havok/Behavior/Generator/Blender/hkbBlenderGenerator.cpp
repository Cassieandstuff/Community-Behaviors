/// @file
/// @brief hkbBlenderGenerator and hkbBlenderGeneratorChild, reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/Generator/Blender/hkbBlenderGenerator.h>

#include <Behavior/Generator/Blender/hkbBlenderGeneratorUtils.h>

namespace CB { namespace havok
{
    hkbBlenderGeneratorChild::hkbBlenderGeneratorChild(const hkbBlenderGeneratorChild& child)
    :   hkbBindable(child),
        m_generator(child.m_generator),
        m_weight(child.m_weight),
        m_worldFromModelWeight(child.m_worldFromModelWeight)
    {
        m_generator->addReference();
        if (child.m_boneWeights) {
            if (child.m_boneWeights->m_variableBindingSet == HK_NULL) {
                m_boneWeights = child.m_boneWeights;
            } else {
                m_boneWeights.setAndDontIncrementRefCount(new hkbBoneWeightArray(*child.m_boneWeights));
            }
        }
    }

    void hkbBlenderGeneratorChild::collectBindables(hkbBindableCollector& collector)
    {
        collector.collectBindable(this);
        if (m_boneWeights) m_boneWeights->collectBindables(collector);
    }

    hkbBlenderGenerator::hkbBlenderGenerator(const hkbBlenderGenerator& gen)
    :   hkbGenerator(gen),
        m_referencePoseWeightThreshold(gen.m_referencePoseWeightThreshold),
        m_blendParameter(gen.m_blendParameter),
        m_minCyclicBlendParameter(gen.m_minCyclicBlendParameter),
        m_maxCyclicBlendParameter(gen.m_maxCyclicBlendParameter),
        m_indexOfSyncMasterChild(gen.m_indexOfSyncMasterChild),
        m_flags(gen.m_flags),
        m_subtractLastChild(gen.m_subtractLastChild),
        m_endIntervalWeight(0.0f),
        m_numActiveChildren(0),
        m_beginIntervalIndex(0),
        m_endIntervalIndex(0),
        m_initSync(false),
        m_doSubtractiveBlend(false)
    {
        const int numChildren = gen.m_children.getSize();
        m_children.reserveExactly(numChildren);
        for (int i = 0; i < numChildren; i++) m_children.pushBack(new hkbBlenderGeneratorChild(*gen.m_children[i]));
    }

    hkbBlenderGenerator::~hkbBlenderGenerator()
    {
        if (getAllocatedSize() != 0) {
            for (int i = 0; i < m_children.getSize(); i++) {
                if (m_children[i]) {
                    if (m_children[i]->m_generator) m_children[i]->m_generator->removeReference();
                    m_children[i]->removeReference();
                }
            }
        }
    }

    const void* hkbBlenderGenerator::getClassType() const
    {
        return &staticClass();
    }

    void hkbBlenderGenerator::collectBindables(hkbBindableCollector& collector)
    {
        collector.collectBindable(this);
        const int numChildren = m_children.getSize();
        for (int i = 0; i < numChildren; i++) m_children[i]->collectBindables(collector);
    }

    /// @note Vanilla keeps `flag | true` unfolded (setnz; or 1); VS2015 24247 folds it to a constant store (rule H).
    void hkbBlenderGenerator::activate(const hkbContext& context)
    {
        m_beginIntervalIndex = 0;
        m_endIntervalIndex   = 0;
        m_endIntervalWeight  = 0.0f;
        m_initSync           = true;

        const int numChildren = m_children.getSize();
        m_childrenInternalStates.reserveExactly(numChildren);
        m_childrenInternalStates.setSize(numChildren);
        hkString::memSet(m_childrenInternalStates.begin(), 0, numChildren * int(sizeof(ChildInternalState)));

        if (updateActiveChildren()) {
            hkbBehaviorGraph* rootBehavior    = context.m_character->m_behaviorGraph;
            rootBehavior->m_updateActiveNodes = rootBehavior->m_updateActiveNodes | true;
        }
    }

    /// @note The same `flag | true` fold as activate (rule H).
    void hkbBlenderGenerator::update(const hkbContext& context, hkReal timestep)
    {
        HK_TIMER_BEGIN("hkbBlenderGenerator::update");

        hkbBehaviorGraph* rootBehavior = context.m_character->m_behaviorGraph;
        const hkbNode*    node         = this;
        if (m_cloneState == CLONE_STATE_CLONE) node = rootBehavior->getNodeTemplate(this);
        const int    index    = rootBehavior->m_activeNodeTemplateToIndexMap->getWithDefault(node, 0);
        hkbNodeInfo* nodeInfo = &(*rootBehavior->m_activeNodes)[index];
        if (nodeInfo->m_isProxy) nodeInfo = &(*rootBehavior->m_activeNodes)[nodeInfo->m_proxyIndex];

        hkbGeneratorSyncInfo& syncInfo = nodeInfo->m_syncInfo;
        syncInfo.m_localTime += timestep;
        if (syncInfo.m_baseFrequency != 0.0f) {
            const hkReal period = 1.0f / syncInfo.m_baseFrequency;
            if (syncInfo.m_localTime > period) syncInfo.m_localTime -= static_cast<int>(syncInfo.m_localTime / period) * period;
        }

        if (updateActiveChildren()) {
            hkbBehaviorGraph* graph    = context.m_character->m_behaviorGraph;
            graph->m_updateActiveNodes = graph->m_updateActiveNodes | true;
        }

        HK_TIMER_END();
    }

    void hkbBlenderGenerator::deactivate(const hkbContext& context)
    {
        m_sortedChildren.clearAndDeallocate();
        const int numChildren = m_childrenInternalStates.getSize();
        for (int i = 0; i < numChildren; i++) m_childrenInternalStates[i].m_isActive = false;
        m_numActiveChildren = 0;
    }

    int hkbBlenderGenerator::getMaxNumChildren(GetChildrenFlags flags)
    {
        if (flags & FLAG_ACTIVE_ONLY) return m_numActiveChildren;
        return m_children.getSize();
    }

    void hkbBlenderGenerator::getChildren(GetChildrenFlags flags, ChildrenInfo& childrenInfo)
    {
        const bool allActive   = (m_flags & FLAG_SYNC) && (flags & FLAG_ACTIVE_ONLY);
        const int  numChildren = m_children.getSize();
        int        numReported = 0;
        for (int i = 0; i < numChildren; i++) {
            if (!(flags & FLAG_ACTIVE_ONLY) || m_childrenInternalStates[i].m_isActive) {
                childrenInfo.m_childInfos.setSize(numReported + 1);
                childrenInfo.m_childInfos[numReported].m_node     = m_children[i]->m_generator;
                childrenInfo.m_childInfos[numReported].m_isActive = allActive ? true : (i == m_indexOfSyncMasterChild);
                numReported++;
            }
        }
        childrenInfo.m_anyActive = true;
    }

    bool hkbBlenderGenerator::isValid(hkStringPtr& errorString) const
    {
        const int numChildren = m_children.getSize();
        if (numChildren == 0) {
            errorString = "A blender must have at least one child.";
            return false;
        }
        for (int i = 0; i < numChildren; i++) {
            const hkbBlenderGeneratorChild* child = m_children[i];
            if (child == HK_NULL || child->m_generator == HK_NULL) {
                errorString = "One of the children is unspecified";
                return false;
            }
            if (child->m_worldFromModelWeight < 0.0f || child->m_worldFromModelWeight > 1.0f) {
                errorString = "Motion weight needs to be between 0 and 1.0";
                return false;
            }
            if (!(m_flags & FLAG_PARAMETRIC_BLEND) && (child->m_weight < 0.0f || child->m_weight > 1.0f)) {
                errorString = "Non parametric blend children should have weight between 0.0 and 1.0";
                return false;
            }
        }
        return true;
    }

    hkbNode* hkbBlenderGenerator::cloneNode(hkbBehaviorGraph& rootBehavior) const
    {
        return new hkbBlenderGenerator(*this);
    }

    void hkbBlenderGenerator::generate(const hkbContext& context, const hkbGeneratorOutput** activeChildrenOutput, hkbGeneratorOutput& output,
                                       hkReal timeOffset)
    {
        hkbBlenderGeneratorUtils::generateInternal(*this, activeChildrenOutput, output, context.m_character->m_worldFromModel,
                                                   context.m_character->m_setup->m_animationSkeleton->m_referencePose.begin());
    }

    bool hkbBlenderGenerator::canRecycleOutput() const
    {
        return m_numActiveChildren == 1 && m_referencePoseWeightThreshold == 0.0f;
    }

    void hkbBlenderGenerator::startEcho(hkReal echoDuration)
    {
        m_initSync = true;
    }

    void hkbBlenderGenerator::setChildGenerator(int index, hkbGenerator* generator)
    {
        if (generator) generator->addReference();
        if (m_children.getSize() <= index) {
            m_children.setSize(index + 1, HK_NULL);
            m_children[index] = new hkbBlenderGeneratorChild;
        } else if (m_children[index]->m_generator) {
            m_children[index]->m_generator->removeReference();
        }
        m_children[index]->m_generator = generator;
    }

    bool hkbBlenderGenerator::updateActiveChildren()
    {
        const int numChildren = m_children.getSize();
        bool      changed     = false;
        m_numActiveChildren   = 0;

        if (m_flags & FLAG_PARAMETRIC_BLEND) computeBlendInterval();

        for (int i = 0; i < numChildren; i++) {
            ChildInternalState& state = m_childrenInternalStates[i];
            if (m_flags & FLAG_DONT_DEACTIVATE_CHILDREN_WITH_ZERO_WEIGHTS) {
                state.m_isActive = true;
            } else if (i == m_indexOfSyncMasterChild) {
                state.m_isActive      = true;
                state.m_syncNextFrame = false;
            } else {
                const hkReal weight    = hkbBlenderGeneratorUtils::computeChildWeight(*this, i);
                const bool   wasActive = state.m_isActive;
                const bool   isActive  = (weight != 0.0f);
                state.m_isActive       = isActive;
                state.m_syncNextFrame  = !wasActive && isActive;
                changed                = changed || (wasActive != isActive);
            }
            m_numActiveChildren += state.m_isActive;
        }
        return changed;
    }

    void hkbBlenderGenerator::computeBlendInterval()
    {
        const int numChildren = m_children.getSize();
        if (m_sortedChildren.getSize() != numChildren) {
            m_sortedChildren.reserveExactly(numChildren);
            m_sortedChildren.setSize(numChildren);
            for (hkInt16 i = 0; i < numChildren; i++) m_sortedChildren[i] = i;
        }
        sortChildren(m_sortedChildren.begin(), numChildren, m_children.begin());

        const int isCyclic = m_flags & FLAG_IS_PARAMETRIC_BLEND_CYCLIC;
        hkReal    minParam;
        hkReal    maxParam;
        if (!isCyclic) {
            minParam = m_children[m_sortedChildren[0]]->m_weight;
            maxParam = m_children[m_sortedChildren[numChildren - 1]]->m_weight;
        } else {
            minParam = m_minCyclicBlendParameter;
            maxParam = m_maxCyclicBlendParameter;
        }

        hkReal param = m_blendParameter;
        if (param <= minParam) {
            param = minParam;
        } else if (param >= maxParam) {
            param = isCyclic ? minParam : maxParam;
        }

        int end = 0;
        while (end < numChildren && !(param < m_children[m_sortedChildren[end]]->m_weight)) end++;

        if (end == 0 && isCyclic) {
            const hkReal first  = m_children[m_sortedChildren[0]]->m_weight;
            m_endIntervalWeight = 1.0f - (first - param) / ((m_maxCyclicBlendParameter - m_children[m_sortedChildren[numChildren - 1]]->m_weight) +
                                                            (first - m_minCyclicBlendParameter));
        } else if (end < numChildren) {
            const hkReal begin  = m_children[m_sortedChildren[end - 1]]->m_weight;
            m_endIntervalWeight = (param - begin) / (m_children[m_sortedChildren[end]]->m_weight - begin);
        } else if (!isCyclic) {
            end                 = static_cast<hkInt16>(numChildren - 1);
            m_endIntervalWeight = 1.0f;
        } else {
            end                 = 0;
            const hkReal last   = m_children[m_sortedChildren[numChildren - 1]]->m_weight;
            m_endIntervalWeight = (param - last) / ((m_children[m_sortedChildren[0]]->m_weight - m_minCyclicBlendParameter) +
                                                    (m_maxCyclicBlendParameter - last));
        }

        m_endIntervalIndex = m_sortedChildren[end];
        if (end == 0) end = numChildren;
        m_beginIntervalIndex = m_sortedChildren[end - 1];
    }

    int hkbBlenderGenerator::computeCommonSyncPointIds(hkbBehaviorGraph& rootBehavior, int* syncPointIdsOut)
    {
        hkLocalBuffer<const hkbGeneratorSyncInfo*> childSyncInfos(m_numActiveChildren);
        int                                        numActive   = 0;
        const int                                  numChildren = m_children.getSize();
        for (int i = 0; i < numChildren; i++) {
            if (m_childrenInternalStates[i].m_isActive) {
                const hkbNode* child = m_children[i]->m_generator;
                if (child->m_cloneState == CLONE_STATE_CLONE) child = rootBehavior.getNodeTemplate(child);
                const int          index     = rootBehavior.m_activeNodeTemplateToIndexMap->getWithDefault(child, 0);
                const hkbNodeInfo* childInfo = &(*rootBehavior.m_activeNodes)[index];
                if (childInfo->m_isProxy) childInfo = &(*rootBehavior.m_activeNodes)[childInfo->m_proxyIndex];
                childSyncInfos[numActive++] = &childInfo->m_syncInfo;
            }
        }
        return hkbBlenderGeneratorUtils::findCommonSyncPointIds(&childSyncInfos[0], numActive, syncPointIdsOut);
    }

    void hkbBlenderGenerator::sortChildren(hkInt16* sortedChildren, int numChildren, hkbBlenderGeneratorChild* const* children)
    {
        bool swapped = true;
        for (int i = 1; i < numChildren; i++) {
            if (!swapped) return;
            swapped = false;
            for (int j = 0; j < numChildren - i; j++) {
                const hkInt16 a = sortedChildren[j];
                const hkInt16 b = sortedChildren[j + 1];
                if (children[b]->m_weight < children[a]->m_weight) {
                    sortedChildren[j + 1] = a;
                    swapped               = true;
                    sortedChildren[j]     = b;
                }
            }
        }
    }

    hkReferencedObject* hkbBlenderGenerator::createInternalState()
    {
        return new hkbBlenderGeneratorInternalState;
    }

    void hkbBlenderGenerator::getInternalState(hkReferencedObject& internalState) const
    {
        hkbBlenderGeneratorInternalState& s = static_cast<hkbBlenderGeneratorInternalState&>(internalState);
        s.m_childrenInternalStates           = m_childrenInternalStates;
        s.m_sortedChildren                   = m_sortedChildren;
        s.m_endIntervalWeight                = m_endIntervalWeight;
        s.m_numActiveChildren                = m_numActiveChildren;
        s.m_beginIntervalIndex               = m_beginIntervalIndex;
        s.m_endIntervalIndex                 = m_endIntervalIndex;
        s.m_initSync                         = m_initSync;
        s.m_doSubtractiveBlend               = m_doSubtractiveBlend;
    }

    void hkbBlenderGenerator::setInternalState(const hkReferencedObject& internalState)
    {
        const hkbBlenderGeneratorInternalState& s = static_cast<const hkbBlenderGeneratorInternalState&>(internalState);
        m_childrenInternalStates                  = s.m_childrenInternalStates;
        m_sortedChildren                          = s.m_sortedChildren;
        m_endIntervalWeight                       = s.m_endIntervalWeight;
        m_numActiveChildren                       = s.m_numActiveChildren;
        m_beginIntervalIndex                      = s.m_beginIntervalIndex;
        m_endIntervalIndex                        = s.m_endIntervalIndex;
        m_initSync                                = s.m_initSync;
        m_doSubtractiveBlend                      = s.m_doSubtractiveBlend;
    }
} }
