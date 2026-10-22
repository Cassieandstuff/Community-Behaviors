/// @file
/// @brief Workbench: the functions of hkbBlenderGenerator.cpp still being matched (tools/cbmatch/parked.yaml says why each is
///        stuck). Compiled only by tools/cbmatch/manifests/workbench.yaml; a body that matches graduates back into
///        havok/Behavior/Generator/Blender/hkbBlenderGenerator.cpp.
#include <Behavior/Generator/Blender/hkbBlenderGenerator.h>
#include <Behavior/Generator/Blender/hkbBlenderGeneratorUtils.h>

namespace CB { namespace havok
{
    void hkbBlenderGenerator::updateSync(const hkbContext& context, hkbNodeInfo& info)
    {
        HK_TIMER_BEGIN("hkbBlenderGenerator::updateSync");

        int               numAdditive     = 0;
        bool              lastIsAdditive  = false;
        int               lastActiveIndex = -1;
        hkbBehaviorGraph* rootBehavior    = context.m_character->m_behaviorGraph;
        const int         numChildren     = m_children.getSize();

        if (m_flags & FLAG_SYNC) {
            hkLocalBuffer<const hkbGeneratorSyncInfo*> childSyncInfos(m_numActiveChildren);
            hkLocalBuffer<hkReal>                      childWeights(m_numActiveChildren);
            int                                        numActive = 0;

            for (int i = 0; i < numChildren; i++) {
                if (m_childrenInternalStates[i].m_isActive) {
                    const hkbNode* child = m_children[i]->m_generator;
                    if (child->m_cloneState == CLONE_STATE_CLONE) child = rootBehavior->getNodeTemplate(child);
                    const int          index     = rootBehavior->m_activeNodeTemplateToIndexMap->getWithDefault(child, 0);
                    const hkbNodeInfo* childInfo = &(*rootBehavior->m_activeNodes)[index];
                    if (childInfo->m_isProxy) childInfo = &(*rootBehavior->m_activeNodes)[childInfo->m_proxyIndex];
                    childSyncInfos[numActive] = &childInfo->m_syncInfo;
                    lastIsAdditive            = childInfo->m_syncInfo.m_isAdditive;
                    numAdditive += lastIsAdditive;

                    if (m_indexOfSyncMasterChild != -1) {
                        childWeights[numActive] = (i == m_indexOfSyncMasterChild) ? 1.0f : 0.0f;
                    } else if (lastIsAdditive || (i == numChildren - 1 && m_subtractLastChild)) {
                        childWeights[numActive] = 0.0f;
                    } else {
                        childWeights[numActive] = hkbBlenderGeneratorUtils::computeChildWeight(*this, i);
                    }
                    numActive++;
                    lastActiveIndex = i;
                }
            }

            if (numActive == 0) {
                info.m_syncInfo.m_playbackSpeed = 1.0f;
                info.m_syncInfo.m_numSyncPoints = 0;
                info.m_syncInfo.m_isCyclic      = false;
                info.m_syncInfo.m_isMirrored    = false;
                info.m_syncInfo.m_isAdditive    = false;
                info.m_syncInfo.m_baseFrequency = 0.0f;
                info.m_syncInfo.m_localTime     = 0.0f;
            } else {
                hkbBlenderGeneratorUtils::computeSyncInfo(&childSyncInfos[0], numActive, &childWeights[0], m_referencePoseWeightThreshold, m_initSync,
                                                          info.m_syncInfo);
                m_initSync = false;

                for (int i = 0; i < numChildren; i++) {
                    if (m_childrenInternalStates[i].m_isActive) {
                        const hkbNode* child = m_children[i]->m_generator;
                        if (child->m_cloneState == CLONE_STATE_CLONE) child = rootBehavior->getNodeTemplate(child);
                        const int          index     = rootBehavior->m_activeNodeTemplateToIndexMap->getWithDefault(child, 0);
                        const hkbNodeInfo* childInfo = &(*rootBehavior->m_activeNodes)[index];
                        if (childInfo->m_isProxy) childInfo = &(*rootBehavior->m_activeNodes)[childInfo->m_proxyIndex];

                        if (m_childrenInternalStates[i].m_syncNextFrame && childInfo->m_syncInfo.m_baseFrequency != 0.0f &&
                            info.m_syncInfo.m_baseFrequency != 0.0f) {
                            hkbGeneratorSyncInfo::SyncPointInterval interval;
                            info.m_syncInfo.computeSyncPointInterval(info.m_syncInfo.m_localTime, interval);
                            hkReal childLocalTime;
                            if (childInfo->m_syncInfo.computeLocalTime(interval, childLocalTime) == HK_SUCCESS) {
                                rootBehavior->setNodeLocalTime(m_children[i]->m_generator, childLocalTime);
                            }
                            m_childrenInternalStates[i].m_syncNextFrame = false;
                        }
                    }
                }
            }
        } else {
            if (m_indexOfSyncMasterChild == -1) {
                info.m_syncInfo.m_playbackSpeed = 1.0f;
                info.m_syncInfo.m_numSyncPoints = 0;
                info.m_syncInfo.m_isCyclic      = false;
                info.m_syncInfo.m_isMirrored    = false;
                info.m_syncInfo.m_isAdditive    = false;
                info.m_syncInfo.m_baseFrequency = 0.0f;
                info.m_syncInfo.m_localTime     = 0.0f;
            } else {
                const hkbNode* child = m_children[m_indexOfSyncMasterChild]->m_generator;
                if (child->m_cloneState == CLONE_STATE_CLONE) child = rootBehavior->getNodeTemplate(child);
                const int          index     = rootBehavior->m_activeNodeTemplateToIndexMap->getWithDefault(child, 0);
                const hkbNodeInfo* childInfo = &(*rootBehavior->m_activeNodes)[index];
                if (childInfo->m_isProxy) childInfo = &(*rootBehavior->m_activeNodes)[childInfo->m_proxyIndex];
                hkString::memCpy(&info.m_syncInfo, &childInfo->m_syncInfo, sizeof(hkbGeneratorSyncInfo));
                info.m_syncInfo.m_isMirrored = false;
            }

            for (int i = 0; i < numChildren; i++) {
                if (m_childrenInternalStates[i].m_isActive) {
                    const hkbNode* child = m_children[i]->m_generator;
                    if (child->m_cloneState == CLONE_STATE_CLONE) child = rootBehavior->getNodeTemplate(child);
                    const int          index     = rootBehavior->m_activeNodeTemplateToIndexMap->getWithDefault(child, 0);
                    const hkbNodeInfo* childInfo = &(*rootBehavior->m_activeNodes)[index];
                    if (childInfo->m_isProxy) childInfo = &(*rootBehavior->m_activeNodes)[childInfo->m_proxyIndex];
                    lastIsAdditive = childInfo->m_syncInfo.m_isAdditive;
                    numAdditive += lastIsAdditive;
                    lastActiveIndex = i;
                }
            }
        }

        m_doSubtractiveBlend = m_subtractLastChild && lastActiveIndex == numChildren - 1 && !lastIsAdditive &&
                               m_numActiveChildren - numAdditive > 1;
        info.m_syncInfo.m_isAdditive = (numAdditive == m_numActiveChildren) || m_doSubtractiveBlend;

        HK_TIMER_END();
    }

    void hkbBlenderGenerator::setChildBoneWeight(int childIndex, int boneIndex, hkReal boneWeight)
    {
        hkbBlenderGeneratorChild* child = m_children[childIndex];
        if (!child->m_boneWeights) child->m_boneWeights.setAndDontIncrementRefCount(new hkbBoneWeightArray);

        hkbBoneWeightArray* boneWeights = child->m_boneWeights;
        if (boneWeights->m_boneWeights.getSize() <= boneIndex) boneWeights->m_boneWeights.setSize(boneIndex + 1, 1.0f);
        if (boneWeight <= 0.0f) boneWeight = 0.0f;
        if (boneWeight >= 1.0f) boneWeight = 1.0f;
        boneWeights->m_boneWeights[boneIndex] = boneWeight;

        int last = boneWeights->m_boneWeights.getSize() - 1;
        for (; last >= 0; last--) {
            if (boneWeights->m_boneWeights[last] != 1.0f) break;
        }
        boneWeights->m_boneWeights.setSize(last + 1);
    }

} }
