/// @file
/// @brief hkbPoseMatchingGenerator, reimplemented against vanilla (AE 1.6.1170).
#include <Behavior/Generator/PoseMatching/hkbPoseMatchingGenerator.h>

#include <Animation/Mapper/hkaSkeletonMapper.h>
#include <Animation/Ragdoll/Instance/hkaRagdollInstance.h>
#include <Animation/Rig/hkaPoseMatchingUtility.h>
#include <Behavior/Driver/Ragdoll/hkbRagdollDriver.h>
#include <Behavior/Generator/hkbGeneratorOutput.h>
#include <Physics/Dynamics/Entity/hkpRigidBody.h>

namespace CB { namespace havok
{
    hkbPoseMatchingGenerator::hkbPoseMatchingGenerator()
    :   m_blendSpeed(1.0f),
        m_minSpeedToSwitch(0.2f),
        m_minSwitchTimeNoError(0.2f),
        m_minSwitchTimeFullError(0.0f),
        m_startPlayingEventId(-1),
        m_startMatchingEventId(-1),
        m_rootBoneIndex(-1),
        m_otherBoneIndex(-1),
        m_anotherBoneIndex(-1),
        m_pelvisIndex(-1),
        m_mode(MODE_MATCH),
        m_currentMatch(-1),
        m_bestMatch(-1),
        m_timeSinceBetterMatch(0.0f),
        m_error(0.0f),
        m_resetCurrentMatchLocalTime(false),
        m_poseMatchingUtility(HK_NULL)
    {
        m_worldFromModelRotation.setIdentity();
    }

    /// @note The settings (rotation through m_mode) copy as one 0x31-byte block.
    hkbPoseMatchingGenerator::hkbPoseMatchingGenerator(const hkbPoseMatchingGenerator& gen)
    :   hkbBlenderGenerator(gen),
        m_currentMatch(-1),
        m_bestMatch(-1),
        m_timeSinceBetterMatch(0.0f),
        m_error(0.0f),
        m_resetCurrentMatchLocalTime(false),
        m_poseMatchingUtility(HK_NULL)
    {
        hkString::memCpy(&m_worldFromModelRotation, &gen.m_worldFromModelRotation,
                         int(reinterpret_cast<const char*>(&gen.m_mode + 1) - reinterpret_cast<const char*>(&gen.m_worldFromModelRotation)));
    }

    hkbPoseMatchingGenerator::~hkbPoseMatchingGenerator()
    {
        delete m_poseMatchingUtility;
    }

    const void* hkbPoseMatchingGenerator::getClassType() const
    {
        return &staticClass();
    }

    void hkbPoseMatchingGenerator::activate(const hkbContext& context)
    {
        m_error        = 1.0f;
        m_currentMatch = -1;
        m_bestMatch    = -1;
        m_mode         = MODE_MATCH;

        hkaRagdollInstance* ragdoll = (context.m_character->m_ragdollDriver == HK_NULL) ? HK_NULL : context.m_character->m_ragdollDriver->getRagdoll();
        if (m_poseMatchingUtility == HK_NULL && ragdoll) {
            hkbContext poseContext(context);
            poseContext.m_nodeToIndexMap = HK_NULL;

            hkbCharacterSetup* setup    = context.m_character->m_setup;
            const hkInt16      numBones = hkInt16(ragdoll->m_skeleton->m_bones.getSize());
            if (m_rootBoneIndex >= 0 && m_rootBoneIndex < numBones && m_otherBoneIndex >= 0 && m_otherBoneIndex < numBones && m_anotherBoneIndex >= 0 &&
                m_anotherBoneIndex < numBones && m_rootBoneIndex != m_otherBoneIndex && m_otherBoneIndex != m_anotherBoneIndex &&
                m_anotherBoneIndex != m_rootBoneIndex) {
                m_poseMatchingUtility = new hkaPoseMatchingUtility(m_rootBoneIndex, m_otherBoneIndex, m_anotherBoneIndex, setup->m_data->m_modelUpMS);

                const hkaSkeleton* animationSkeleton = setup->m_animationSkeleton;
                const int          numAnimationBones = animationSkeleton->m_bones.getSize();
                hkbGeneratorOutput output(numAnimationBones, 0, 0, 0, 0x15, 0);

                const hkLong numChildren = m_children.getSize();
                for (hkLong i = 0; i < numChildren; i++) {
                    hkbBehaviorGraph* graph;
                    if (context.m_character->m_behaviorGraph->m_nodeTemplateToCloneMap) {
                        graph = context.m_behavior->cloneShallow(HK_NULL);
                        graph->setRootGenerator(m_children.m_data[i]->m_generator);
                        graph->setData(HK_NULL);
                    } else {
                        graph = new hkbBehaviorGraph(m_children.m_data[i]->m_generator, HK_NULL);
                    }

                    hkbBehaviorGraph* behavior = context.m_character->m_behaviorGraph;
                    if (behavior) behavior->addReference();
                    context.m_character->setBehavior(graph);
                    graph->m_userData        = behavior->m_userData;
                    poseContext.m_behavior   = HK_NULL;
                    graph->activate(poseContext);
                    graph->generate(poseContext, output, false, 0.0f, false);
                    graph->deactivate(poseContext);

                    for (int t = 0; t < output.getNumTracks(); t++) {
                        output.m_tracks->m_trackHeaders[t].m_onFraction = 0.0f;
                        output.m_tracks->m_trackHeaders[t].m_flags &= ~hkbGeneratorOutput::TRACK_FLAG_ADDITIVE_POSE;
                        if (output.m_tracks->m_trackHeaders[t].m_flags & (hkbGeneratorOutput::TRACK_FLAG_PALETTE | hkbGeneratorOutput::TRACK_FLAG_SPARSE)) {
                            output.m_tracks->m_trackHeaders[t].m_numData = 0;
                        }
                    }

                    context.m_character->setBehavior(behavior);
                    graph->removeReference();

                    {
                        hkLocalBuffer<hkQsTransform> animationPoseModel(numAnimationBones);
                        hkaSkeletonUtils::transformLocalPoseToModelPose(animationSkeleton->m_bones.getSize(), animationSkeleton->m_parentIndices.begin(),
                                                                        output.accessPoseLocal(), &animationPoseModel[0]);
                        const hkaSkeleton*           ragdollSkeleton = ragdoll->m_skeleton;
                        hkLocalBuffer<hkQsTransform> ragdollPoseModel(ragdollSkeleton->m_bones.getSize());
                        hkaSkeletonUtils::transformLocalPoseToModelPose(ragdollSkeleton->m_bones.getSize(), ragdollSkeleton->m_parentIndices.begin(),
                                                                        ragdollSkeleton->m_referencePose.begin(), &ragdollPoseModel[0]);
                        setup->m_animationToRagdollSkeletonMapper->mapPose(&animationPoseModel[0], ragdollSkeleton->m_referencePose.begin(), &ragdollPoseModel[0],
                                                                          hkaSkeletonMapper::CURRENT_POSE);
                        m_poseMatchingUtility->addCandidatePose(&ragdollPoseModel[0], 0, 0.0f);
                    }
                    behavior->removeReference();
                }
            }
        }

        for (int i = 0; i < m_children.getSize(); i++) m_children[i]->m_weight = 0.0f;
        matchPose(context, 0.0f);
        hkbBlenderGenerator::activate(context);
    }

    void hkbPoseMatchingGenerator::update(const hkbContext& context, hkReal timestep)
    {
        matchPose(context, timestep);
        hkbBlenderGenerator::update(context, timestep);
    }

    void hkbPoseMatchingGenerator::handleEvent(const hkbContext& context, const hkbEvent e)
    {
        if (e.getId() == m_startPlayingEventId) {
            m_mode = MODE_PLAY;
        } else if (e.getId() == m_startMatchingEventId) {
            m_resetCurrentMatchLocalTime = true;
            m_mode                       = MODE_MATCH;
        }
    }

    void hkbPoseMatchingGenerator::deactivate(const hkbContext& context)
    {
        hkbBlenderGenerator::deactivate(context);
        delete m_poseMatchingUtility;
        m_poseMatchingUtility = HK_NULL;
    }

    bool hkbPoseMatchingGenerator::isValid(hkStringPtr& errorString) const
    {
        if (!hkbBlenderGenerator::isValid(errorString)) return false;

        const hkbVariableBindingSet* bindings = m_variableBindingSet;
        const bool rootValid    = m_rootBoneIndex >= 0 || (bindings && bindings->findBindingByMemberPath("rootBoneIndex"));
        const bool otherValid   = m_otherBoneIndex >= 0 || (bindings && bindings->findBindingByMemberPath("otherBoneIndex"));
        const bool anotherValid = m_anotherBoneIndex >= 0 || (bindings && bindings->findBindingByMemberPath("anotherBoneIndex"));
        const bool pelvisValid  = m_pelvisIndex >= 0 || (bindings && bindings->findBindingByMemberPath("pelvisIndex"));

        if (rootValid + otherValid + anotherValid + pelvisValid < 4) {
            errorString = "You must specify all four bone indices.";
            return false;
        }
        if (m_rootBoneIndex >= 0 && m_otherBoneIndex >= 0 && m_anotherBoneIndex >= 0 &&
                (m_rootBoneIndex == m_otherBoneIndex || m_rootBoneIndex == m_anotherBoneIndex || m_otherBoneIndex == m_anotherBoneIndex)) {
            errorString = "You must specify unique bone indices for the pose matching bones.";
            return false;
        }
        return true;
    }

    hkbNode* hkbPoseMatchingGenerator::cloneNode(hkbBehaviorGraph& rootBehavior) const
    {
        return new hkbPoseMatchingGenerator(*this);
    }

    void hkbPoseMatchingGenerator::getActiveEvents(hkPointerMap<int, bool>& activeEvents) const
    {
        if (m_startPlayingEventId >= 0) activeEvents.insert(m_startPlayingEventId, true);
    }

    void hkbPoseMatchingGenerator::updateSync(const hkbContext& context, hkbNodeInfo& info)
    {
        if (m_mode == MODE_PLAY) {
            hkbBlenderGenerator::updateSync(context, info);
            return;
        }

        hkbNodeInfo* nodeInfo = &info;
        if (nodeInfo->m_isProxy) nodeInfo = &(*context.m_character->m_behaviorGraph->m_activeNodes)[nodeInfo->m_proxyIndex];
        nodeInfo->m_syncInfo.m_playbackSpeed = 1.0f;
        nodeInfo->m_syncInfo.m_numSyncPoints = 0;
        nodeInfo->m_syncInfo.m_isCyclic      = false;
        nodeInfo->m_syncInfo.m_isMirrored    = false;
        nodeInfo->m_syncInfo.m_isAdditive    = false;
        nodeInfo->m_syncInfo.m_baseFrequency = 0.0f;
        nodeInfo->m_syncInfo.m_localTime     = 0.0f;

        hkbBehaviorGraph* rootBehavior = context.m_character->m_behaviorGraph;
        for (int i = 0; i < m_children.getSize(); i++) {
            if (m_childrenInternalStates[i].m_isActive) {
                hkbNodeInfo& childInfo           = rootBehavior->getNodeInfo(m_children[i]->m_generator);
                childInfo.m_syncInfo.m_baseFrequency = 0.0f;
                childInfo.m_syncInfo.m_localTime     = 0.0f;
                childInfo.m_syncInfo.m_playbackSpeed = 0.0f;
                childInfo.m_syncInfo.m_numSyncPoints = 0;
                childInfo.m_syncInfo.m_isCyclic      = false;
                childInfo.m_syncInfo.m_isMirrored    = false;
                childInfo.m_syncInfo.m_isAdditive    = false;
                if (i == m_currentMatch && m_resetCurrentMatchLocalTime) {
                    rootBehavior->setNodeLocalTime(m_children[i]->m_generator, 0.0f);
                    m_resetCurrentMatchLocalTime = false;
                }
            }
        }
    }

    void hkbPoseMatchingGenerator::matchPose(const hkbContext& context, hkReal timestep)
    {
        if (m_mode == MODE_MATCH && m_poseMatchingUtility && context.m_character->m_ragdollDriver) {
            hkaRagdollInstance* ragdoll = context.m_character->m_ragdollDriver->getRagdoll();
            if (ragdoll) {
                const hkaSkeleton* ragdollSkeleton = ragdoll->m_skeleton;

                hkQsTransform worldFromModel;
                worldFromModel.m_translation.setZero4();
                worldFromModel.m_scale.setAll(1.0f);
                worldFromModel.m_rotation = m_worldFromModelRotation;

                hkLocalBuffer<hkQsTransform> ragdollPoseModel(ragdollSkeleton->m_bones.getSize());
                ragdoll->getPoseModelSpace(&ragdollPoseModel[0], worldFromModel);
                hkLocalBuffer<hkQsTransform> scaledPoseModel(ragdollSkeleton->m_bones.getSize());
                hkbRagdollDriver::copyAndScalePose(false, ragdollSkeleton->m_bones.getSize(), &ragdollPoseModel[0], &scaledPoseModel[0], 1.0f);

                int bestMatch = m_poseMatchingUtility->findBestCandidatePoseIndex(&scaledPoseModel[0], m_error);
                if (bestMatch < 0) bestMatch = 0;

                if (m_currentMatch == -1) {
                    m_currentMatch = bestMatch;
                    for (int i = 0; i < m_children.getSize(); i++) m_children[i]->m_weight = 0.0f;
                    m_children[m_currentMatch]->m_weight = 1.0f;
                } else {
                    if (bestMatch == m_bestMatch) {
                        m_timeSinceBetterMatch += timestep;
                    } else {
                        m_timeSinceBetterMatch = 0.0f;
                    }
                    m_bestMatch = bestMatch;

                    bool switchMatch;
                    if (bestMatch == m_currentMatch ||
                        m_timeSinceBetterMatch <= (1.0f - m_error) * m_minSwitchTimeNoError + m_error * m_minSwitchTimeFullError) {
                        switchMatch = false;
                    } else {
                        switchMatch = true;
                    }

                    hkReal pelvisSpeed = 0.0f;
                    if (m_pelvisIndex >= 0 && m_pelvisIndex < ragdollSkeleton->m_bones.getSize()) {
                        const int rigidBodyIndex = ragdoll->m_boneToRigidBodyMap[m_pelvisIndex];
                        if (rigidBodyIndex >= 0 && ragdoll->m_rigidBodies[rigidBodyIndex]) {
                            const hkpRigidBody* pelvis = ragdoll->getRigidBodyOfBone(m_pelvisIndex);
                            pelvisSpeed                = pelvis->getLinearVelocity().length3().getReal();
                        }
                    }
                    if (pelvisSpeed >= m_minSpeedToSwitch && switchMatch) {
                        m_currentMatch         = bestMatch;
                        m_timeSinceBetterMatch = 0.0f;
                    }
                }
            }
        }

        for (int i = 0; i < m_children.getSize(); i++) {
            const hkReal              delta = timestep * m_blendSpeed;
            hkbBlenderGeneratorChild* child = m_children[i];
            if (i == m_currentMatch) {
                child->m_weight = hkMath::min2(child->m_weight + delta, 1.0f);
            } else {
                child->m_weight = hkMath::max2(child->m_weight - delta, 0.0f);
            }
        }
    }

    hkReferencedObject* hkbPoseMatchingGenerator::createInternalState()
    {
        return new hkbPoseMatchingGeneratorInternalState;
    }

    void hkbPoseMatchingGenerator::getInternalState(hkReferencedObject& internalState) const
    {
        hkbPoseMatchingGeneratorInternalState& s = static_cast<hkbPoseMatchingGeneratorInternalState&>(internalState);
        s.m_currentMatch                         = m_currentMatch;
        s.m_bestMatch                            = m_bestMatch;
        s.m_timeSinceBetterMatch                 = m_timeSinceBetterMatch;
        s.m_error                                = m_error;
        s.m_resetCurrentMatchLocalTime           = m_resetCurrentMatchLocalTime;
    }

    void hkbPoseMatchingGenerator::setInternalState(const hkReferencedObject& internalState)
    {
        const hkbPoseMatchingGeneratorInternalState& s = static_cast<const hkbPoseMatchingGeneratorInternalState&>(internalState);
        m_currentMatch                                 = s.m_currentMatch;
        m_bestMatch                                    = s.m_bestMatch;
        m_timeSinceBetterMatch                         = s.m_timeSinceBetterMatch;
        m_error                                        = s.m_error;
        m_resetCurrentMatchLocalTime                   = s.m_resetCurrentMatchLocalTime;
    }
} }
