/// @file
/// @brief BSiStateTaggingGenerator, reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/Generator/BSiStateTaggingGenerator/BSiStateTaggingGenerator.h>

#include <Behavior/Generator/hkbGeneratorOutputUtils.h>
#include <Common/Base/Container/String/hkString.h>
#include <Common/Base/Monitor/hkMonitorStream.h>

namespace CB { namespace havok
{
    /// @note VARIANT: VS2015 24247 stores hkbNode::m_id after the m_cloneState store and the m_userData bit, vanilla
    ///       before them (schedule only, as in the other node default constructors).
    BSiStateTaggingGenerator::BSiStateTaggingGenerator()
    :   m_pDefaultGenerator(HK_NULL),
        m_iStateToSetAs(0),
        m_iPriority(0)
    {
        m_userData |= 1;
    }

    BSiStateTaggingGenerator::BSiStateTaggingGenerator(hkFinishLoadedObjectFlag flag)
    :   hkbGenerator(flag)
    {
        m_userData |= 1;
    }

    BSiStateTaggingGenerator::BSiStateTaggingGenerator(const BSiStateTaggingGenerator& gen)
    :   hkbGenerator(gen),
        m_pDefaultGenerator(gen.m_pDefaultGenerator),
        m_iStateToSetAs(gen.m_iStateToSetAs),
        m_iPriority(gen.m_iPriority)
    {
        m_userData |= 1;
    }

    const void* BSiStateTaggingGenerator::getClassType() const
    {
        return &staticClass();
    }

    int BSiStateTaggingGenerator::getMaxNumChildren(GetChildrenFlags flags)
    {
        return m_pDefaultGenerator != HK_NULL;
    }

    void BSiStateTaggingGenerator::getChildren(GetChildrenFlags flags, ChildrenInfo& childrenInfo)
    {
        if (m_pDefaultGenerator) {
            childrenInfo.m_childInfos.setSize(1);
            childrenInfo.m_childInfos[0].m_node     = m_pDefaultGenerator;
            childrenInfo.m_childInfos[0].m_isActive = true;
            childrenInfo.m_anyActive                = true;
        }
    }

    /// @note An error already in `errorString` (from the caller) also fails it.
    bool BSiStateTaggingGenerator::isValid(hkStringPtr& errorString) const
    {
        if (!m_pDefaultGenerator) errorString = "Default Generator not set.";
        return errorString.getLength() == 0;
    }

    hkbNode* BSiStateTaggingGenerator::cloneNode(hkbBehaviorGraph& rootBehavior) const
    {
        return new BSiStateTaggingGenerator(*this);
    }

    void BSiStateTaggingGenerator::generate(const hkbContext& context, const hkbGeneratorOutput** activeChildrenOutput, hkbGeneratorOutput& output,
                                            hkReal timeOffset)
    {
        HK_TIMER_BEGIN("BSiStateTaggingGenerator::generate");
        if (&output != activeChildrenOutput[0] && activeChildrenOutput[0])
            hkbGeneratorOutputUtils::copyGeneratorOutput(*activeChildrenOutput[0], output);
        HK_TIMER_END();
    }

    bool BSiStateTaggingGenerator::canRecycleOutput() const
    {
        return true;
    }

    void BSiStateTaggingGenerator::updateSync(const hkbContext& context, hkbNodeInfo& info)
    {
        const hkbNode* child = m_pDefaultGenerator;
        if (child) {
            hkbBehaviorGraph* rootBehavior = context.m_character->m_behaviorGraph;
            if (child->m_cloneState == CLONE_STATE_CLONE) child = rootBehavior->getNodeTemplate(child);
            const int          index     = rootBehavior->m_activeNodeTemplateToIndexMap->getWithDefault(child, 0);
            const hkbNodeInfo* childInfo = &(*rootBehavior->m_activeNodes)[index];
            if (childInfo->m_isProxy) childInfo = &(*rootBehavior->m_activeNodes)[childInfo->m_proxyIndex];
            hkString::memCpy(&info.m_syncInfo, &childInfo->m_syncInfo, sizeof(hkbGeneratorSyncInfo));
            info.m_syncInfo.m_isMirrored = false;
        } else {
            hkbGenerator::updateSync(context, info);
        }
    }
} }
