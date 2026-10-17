/// @file
/// @brief hkbKeyframeBonesModifier, reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/Modifier/KeyframeBones/hkbKeyframeBonesModifier.h>

#include <Behavior/Modifier/KeyframeBones/hkbKeyframeBonesModifierUtils.h>
#include <Common/Base/Container/LocalArray/hkLocalArray.h>
#include <Common/Base/Container/PointerMap/hkPointerMap.h>
#include <Common/Base/Memory/Util/Stats/hkStatisticsCollector.h>
#include <Common/Base/Monitor/hkMonitorStream.h>

namespace CB { namespace schema
{
    hkbKeyframeBonesModifier::hkbKeyframeBonesModifier(const hkbKeyframeBonesModifier& mod)
    :   hkbModifier(mod)
    {
        m_userData |= 1;
        m_keyframeInfo = mod.m_keyframeInfo;
        if (mod.m_keyframedBonesList) {
            if (mod.m_keyframedBonesList->m_variableBindingSet == HK_NULL) {
                m_keyframedBonesList = mod.m_keyframedBonesList;
            } else {
                m_keyframedBonesList.setAndDontIncrementRefCount(new hkbBoneIndexArray(*mod.m_keyframedBonesList));
            }
        }
    }

    void hkbKeyframeBonesModifier::calcContentStatistics(hkStatisticsCollector* collector, const hkClass* cls) const
    {
        hkStatisticsCollectorUtil::addDeclaredMembers(this, &hkbKeyframeBonesModifierClass, collector);
        hkReferencedObject::calcContentStatistics(collector, cls);
    }

    void hkbKeyframeBonesModifier::collectBindables(hkbBindableCollector& collector)
    {
        collector.collectBindable(this);
        if (m_keyframedBonesList) m_keyframedBonesList->collectBindables(collector);
    }

    void hkbKeyframeBonesModifier::activate(const hkbContext& context)
    {
        for (int i = 0; i < m_keyframeInfo.getSize(); i++) m_keyframeInfo[i].m_isValid = false;
    }

    hkbNode* hkbKeyframeBonesModifier::cloneNode(hkbBehaviorGraph& rootBehavior) const
    {
        if (m_variableBindingSet == HK_NULL && (m_keyframedBonesList == HK_NULL || m_keyframedBonesList->m_variableBindingSet == HK_NULL)) {
            hkbKeyframeBonesModifier* self = const_cast<hkbKeyframeBonesModifier*>(this);
            self->m_cloneState             = CLONE_STATE_SHARABLE;
            addReference();
            return self;
        }
        return new hkbKeyframeBonesModifier(*this);
    }

    void hkbKeyframeBonesModifier::modify(const hkbContext& context, hkbGeneratorOutput& inOut)
    {
        HK_TIMER_BEGIN("hkbKeyframeBonesModifier");
        hkbKeyframeBonesModifierUtils::modify(*this, inOut);
        HK_TIMER_END();
    }

    void hkbKeyframeBonesModifier::addKeyframedBone(int boneIndex)
    {
        if (m_keyframedBonesList == HK_NULL) m_keyframedBonesList.setAndDontIncrementRefCount(new hkbBoneIndexArray());
        m_keyframedBonesList->m_boneIndices.pushBack(hkInt16(boneIndex));
    }
} }
