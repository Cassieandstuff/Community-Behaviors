/// @file
/// @brief hkbBehaviorReferenceGenerator, reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/Generator/BehaviorReference/hkbBehaviorReferenceGenerator.h>

#include <Behavior/Generator/hkbGeneratorOutputUtils.h>
#include <Common/Base/Memory/Util/Stats/hkStatisticsCollector.h>

namespace CB { namespace havok
{
    hkbBehaviorReferenceGenerator::hkbBehaviorReferenceGenerator()
    {
    }

    /// @note Bethesda's m_userData gates the release (the graph is released only while it is 0); what a nonzero value
    ///       marks is not identified.
    hkbBehaviorReferenceGenerator::~hkbBehaviorReferenceGenerator()
    {
        if (m_behavior && m_userData == 0) m_behavior->removeReference();
    }

    const void* hkbBehaviorReferenceGenerator::getClassType() const
    {
        return &staticClass();
    }

    void hkbBehaviorReferenceGenerator::calcContentStatistics(hkStatisticsCollector* collector, const hkClass* cls) const
    {
        hkStatisticsCollectorUtil::addDeclaredMembers(this, &hkbBehaviorReferenceGeneratorClass, collector);
        collector->addReferencedObject("hkbBehaviorGraph", m_behavior);
        hkbGenerator::calcContentStatistics(collector, &hkbGeneratorClass);
    }

    /// @note Bit 4 is the flag vanilla tests here; hkbNode's enum names it FLAG_GENERATORS_ONLY after the Havok SDK
    ///       (unverified), but a graph is a generator, so here it can only mean "skip referenced behaviors".
    void hkbBehaviorReferenceGenerator::getChildren(GetChildrenFlags flags, ChildrenInfo& childrenInfo)
    {
        if (m_behavior && !(flags & 4)) {
            childrenInfo.m_childInfos.setSize(1);
            childrenInfo.m_childInfos[0].m_node = m_behavior;
            if (flags & FLAG_ACTIVE_ONLY) {
                childrenInfo.m_childInfos[0].m_isActive = true;
                childrenInfo.m_anyActive                = true;
            }
        }
    }

    bool hkbBehaviorReferenceGenerator::isValid(hkStringPtr& errorString) const
    {
        if (!m_behavior) {
            errorString = "You must provide a behavior.";
            return false;
        }
        return true;
    }

    hkbNode* hkbBehaviorReferenceGenerator::cloneNode(hkbBehaviorGraph& rootBehavior) const
    {
        hkbBehaviorReferenceGenerator* shared = const_cast<hkbBehaviorReferenceGenerator*>(this);
        shared->m_cloneState                  = CLONE_STATE_SHARABLE;
        shared->addReference();
        return shared;
    }

    void hkbBehaviorReferenceGenerator::generate(const hkbContext& context, const hkbGeneratorOutput** activeChildrenOutput,
                                                 hkbGeneratorOutput& output, hkReal timeOffset)
    {
        if (&output != activeChildrenOutput[0]) hkbGeneratorOutputUtils::copyGeneratorOutput(*activeChildrenOutput[0], output);
    }

    bool hkbBehaviorReferenceGenerator::canRecycleOutput() const
    {
        return true;
    }

    /// @note The proxy flag is set before the graph's record is read, as vanilla orders it.
    void hkbBehaviorReferenceGenerator::updateSync(const hkbContext& context, hkbNodeInfo& info)
    {
        hkbBehaviorGraph* rootBehavior = context.m_character->m_behaviorGraph;
        const hkbNode*    behavior     = m_behavior;
        if (behavior->m_cloneState == CLONE_STATE_CLONE) behavior = rootBehavior->getNodeTemplate(behavior);
        const int index = rootBehavior->m_activeNodeTemplateToIndexMap->getWithDefault(behavior, 0);
        info.m_isProxy  = true;
        info.m_proxyIndex =
            (*rootBehavior->m_activeNodes)[index].m_isProxy ? (*rootBehavior->m_activeNodes)[index].m_proxyIndex : index;
    }
} }
