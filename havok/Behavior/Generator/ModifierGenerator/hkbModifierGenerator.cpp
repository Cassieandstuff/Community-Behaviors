/// @file
/// @brief hkbModifierGenerator, reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/Generator/ModifierGenerator/hkbModifierGenerator.h>
#include <Behavior/Generator/hkbGeneratorOutputUtils.h>

#include <Behavior/BehaviorGraph/hkbBehaviorGraph.h>
#include <Behavior/Context/hkbContext.h>
#include <Behavior/Generator/hkbGeneratorOutput.h>
#include <Behavior/Modifier/BSTweenerModifier/BSTweenerModifier.h>
#include <Behavior/Modifier/ModifierList/hkbModifierList.h>

namespace CB { namespace havok
{
    hkbModifierGenerator::hkbModifierGenerator(hkbModifier* modifier, hkbGenerator* generator)
    :   m_modifier(modifier),
        m_generator(generator)
    {
        m_userData |= 1;
    }

    hkbModifierGenerator::hkbModifierGenerator(const hkbModifierGenerator& gen)
    :   hkbGenerator(gen),
        m_modifier(gen.m_modifier),
        m_generator(gen.m_generator)
    {
        m_userData |= 1;
    }

    const void* hkbModifierGenerator::getClassType() const
    {
        return &staticClass();
    }

    void hkbModifierGenerator::getChildren(GetChildrenFlags flags, ChildrenInfo& childrenInfo)
    {
        childrenInfo.m_childInfos.setSize(1);
        childrenInfo.m_childInfos[0].m_node = m_generator;
        if (!(flags & FLAG_GENERATORS_ONLY) && m_modifier) {
            childrenInfo.m_childInfos.setSize(2);
            childrenInfo.m_childInfos[1].m_node = m_modifier;
            if (!(flags & FLAG_ACTIVE_ONLY)) return;
            childrenInfo.m_childInfos[1].m_isActive = false;
        }
        if (flags & FLAG_ACTIVE_ONLY) {
            childrenInfo.m_childInfos[0].m_isActive = true;
            childrenInfo.m_anyActive                     = true;
        }
    }

    bool hkbModifierGenerator::isValid(hkStringPtr& errorString) const
    {
        if (m_generator == HK_NULL) {
            errorString = "You must specify a child generator.";
            return false;
        }
        return true;
    }

    hkbNode* hkbModifierGenerator::cloneNode(hkbBehaviorGraph& rootBehavior) const
    {
        return new hkbModifierGenerator(*this);
    }

    void hkbModifierGenerator::generate(const hkbContext& context, const hkbGeneratorOutput** activeChildrenOutput,
                                        hkbGeneratorOutput& output, hkReal timeOffset)
    {
        HK_TIMER_BEGIN("hkbModifierGenerator::generate");

        if (&output != activeChildrenOutput[0]) {
            HK_TIMER_BEGIN("copy output");
            hkbGeneratorOutputUtils::copyGeneratorOutput(*activeChildrenOutput[0], output);
            HK_TIMER_END();
        }

        if (timeOffset == 0.0f && m_modifier) {
            HK_TIMER_BEGIN("call modify");
            hkbModifier::callModify(context, m_modifier, output);
            HK_TIMER_END();
        }

        HK_TIMER_END();
    }

    void hkbModifierGenerator::updateSync(const hkbContext& context, hkbNodeInfo& info)
    {
        hkbBehaviorGraph* rootBehavior = context.m_character->m_behaviorGraph;
        hkbNodeInfo*      nodeInfo     = info.m_isProxy ? &(*rootBehavior->m_activeNodes)[info.m_proxyIndex] : &info;

        const hkbNode* generator = m_generator;
        if (generator->m_cloneState == CLONE_STATE_CLONE) generator = rootBehavior->getNodeTemplate(generator);
        const int    index     = rootBehavior->m_activeNodeTemplateToIndexMap->getWithDefault(generator, 0);
        hkbNodeInfo* childInfo = &(*rootBehavior->m_activeNodes)[index];
        if (childInfo->m_isProxy) childInfo = &(*rootBehavior->m_activeNodes)[childInfo->m_proxyIndex];

        hkString::memCpy(&nodeInfo->m_syncInfo, &childInfo->m_syncInfo, sizeof(hkbGeneratorSyncInfo));
        nodeInfo->m_syncInfo.m_isMirrored = false;

        hkbModifier* modifier = m_modifier;
        if (modifier && modifier->m_enable) {
            if (modifier->getClassType() == &hkbModifierList::staticClass() || m_modifier->getClassType() == &BSTweenerModifier::staticClass()) {
                hkbModifier* clone = static_cast<hkbModifier*>(context.m_character->m_behaviorGraph->getNodeClone(m_modifier));
                clone->updateSync(context, *nodeInfo);
            }
        }
    }

    bool hkbModifierGenerator::canRecycleOutput() const
    {
        return true;
    }
} }
