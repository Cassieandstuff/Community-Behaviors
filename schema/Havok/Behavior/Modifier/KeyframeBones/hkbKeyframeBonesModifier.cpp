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

    /// @note A bone list bound to a variable is left out (its contents are not known yet).
    bool hkbKeyframeBonesModifier::isValid(hkStringPtr& errorString) const
    {
        int numBones = 0;
        if (m_variableBindingSet == HK_NULL) numBones = m_keyframeInfo.getSize();
        if (m_keyframedBonesList && m_keyframedBonesList->m_variableBindingSet == HK_NULL) numBones += m_keyframedBonesList->m_boneIndices.getSize();

        if (numBones > 0) {
            hkLocalArray<hkInt16> boneIndices(numBones);
            if (m_variableBindingSet == HK_NULL) {
                for (int i = 0; i < m_keyframeInfo.getSize(); i++) boneIndices.pushBack(m_keyframeInfo[i].m_boneIndex);
            }
            if (m_keyframedBonesList && m_keyframedBonesList->m_variableBindingSet == HK_NULL) {
                for (int i = 0; i < m_keyframedBonesList->m_boneIndices.getSize(); i++) boneIndices.pushBack(m_keyframedBonesList->m_boneIndices[i]);
            }

            const int mapSize = hkPointerMap<int, int>::getSizeInBytesFor(numBones);
            char*     storage = hkAllocateStack<char>(mapSize);
            hkPointerMap<int, int> boneSet(storage, mapSize);
            for (int i = 0; i < boneIndices.getSize(); i++) {
                if (boneIndices[i] == -1) {
                    errorString = "Unspecified bone index.";
                    boneSet.clearAndDeallocate();
                    hkDeallocateStack(storage, mapSize);
                    return false;
                }
                if (boneSet.getWithDefault(boneIndices[i], -1) != -1) {
                    errorString = "Duplicate bone index.";
                    boneSet.clearAndDeallocate();
                    hkDeallocateStack(storage, mapSize);
                    return false;
                }
                boneSet.insert(boneIndices[i], 1);
            }
            boneSet.clearAndDeallocate();
            hkDeallocateStack(storage, mapSize);
        }
        return true;
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
