/// @file
/// @brief Workbench: the functions of hkbKeyframeBonesModifier.cpp still being matched (tools/cbmatch/parked.yaml says why each is
///        stuck). Compiled only by tools/cbmatch/manifests/workbench.yaml; a body that matches graduates back into
///        schema/Havok/Behavior/Modifier/KeyframeBones/hkbKeyframeBonesModifier.cpp.
#include <Behavior/Modifier/KeyframeBones/hkbKeyframeBonesModifier.h>
#include <Behavior/Modifier/KeyframeBones/hkbKeyframeBonesModifierUtils.h>
#include <Common/Base/Container/LocalArray/hkLocalArray.h>
#include <Common/Base/Container/PointerMap/hkPointerMap.h>
#include <Common/Base/Memory/Util/Stats/hkStatisticsCollector.h>
#include <Common/Base/Monitor/hkMonitorStream.h>

namespace CB { namespace schema
{
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

} }
