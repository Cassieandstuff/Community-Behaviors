/// @file
/// @brief hkbBlendingTransitionEffect, reimplemented against vanilla (AE 1.6.1170).
/// @note Vanilla's unit runs 0x140b01200-0x140b02303 (with the hkArray<hkQsTransform>::_append instance at 0x140b02000).
///       The folder is inferred from the exe's TransitionEffect\Generator\hkbGeneratorTransitionEffect.cpp.
#include <Behavior/TransitionEffect/Blending/hkbBlendingTransitionEffect.h>
#include <Behavior/TransitionEffect/Blending/hkbBlendingTransitionEffectUtils.h>

#include <Animation/Rig/hkaSkeleton.h>
#include <Behavior/BehaviorGraph/hkbBehaviorGraph.h>
#include <Behavior/Context/hkbContext.h>
#include <Behavior/Generator/Blender/hkbBlenderGeneratorUtils.h>
#include <Behavior/Project/hkbProjectData.h>
#include <Behavior/Utils/hkbBlendCurveUtils.h>

namespace CB { namespace schema
{
    hkbBlendingTransitionEffect::hkbBlendingTransitionEffect()
    :   m_duration(0.0f),
        m_toGeneratorStartTimeFraction(0.0f),
        m_flags(FLAG_NONE),
        m_endMode(END_MODE_NONE),
        m_blendCurve(hkbBlendCurveUtils::BLEND_CURVE_SMOOTH),
        m_fromGenerator(HK_NULL),
        m_toGenerator(HK_NULL),
        m_timeRemaining(0.0f),
        m_timeInTransition(0.0f),
        m_applySelfTransition(false),
        m_initializeCharacterPose(false)
    {
    }

    hkbBlendingTransitionEffect::hkbBlendingTransitionEffect(const hkbBlendingTransitionEffect& te)
    :   hkbTransitionEffect(te),
        m_duration(te.m_duration),
        m_toGeneratorStartTimeFraction(te.m_toGeneratorStartTimeFraction),
        m_flags(te.m_flags),
        m_endMode(te.m_endMode),
        m_blendCurve(te.m_blendCurve),
        m_fromGenerator(HK_NULL),
        m_toGenerator(HK_NULL),
        m_timeRemaining(0.0f),
        m_timeInTransition(0.0f),
        m_applySelfTransition(false),
        m_initializeCharacterPose(false)
    {
    }

    const void* hkbBlendingTransitionEffect::getClassType() const
    {
        return &staticClass();
    }

    void hkbBlendingTransitionEffect::activate(const hkbContext& context)
    {
        if (m_timeRemaining < HK_REAL_EPSILON) {
            m_timeRemaining       = m_duration;
            m_timeInTransition    = 0.0f;
            m_applySelfTransition = isGeneratorActive(context, m_toGenerator);
        }
        m_defaultEventMode = context.m_character->m_projectData->m_defaultEventMode;
    }

    void hkbBlendingTransitionEffect::update(const hkbContext& context, hkReal timestep)
    {
        updateInternal(context, timestep, this);
    }

    void hkbBlendingTransitionEffect::deactivate(const hkbContext& context)
    {
        m_timeRemaining = 0.0f;
        m_characterPoseAtBeginningOfTransition.clear();
        m_initializeCharacterPose = false;
    }

    void hkbBlendingTransitionEffect::getChildren(GetChildrenFlags flags, ChildrenInfo& childrenInfo)
    {
        if (!(flags & FLAG_ACTIVE_ONLY)) return;

        if (m_applySelfTransition && m_selfTransitionMode == SELF_TRANSITION_MODE_RESET) {
            childrenInfo.m_anyReactivate = true;
            m_applySelfTransition        = false;
        }

        childrenInfo.m_childInfos.setSize(1);
        childrenInfo.m_childInfos[0].m_node = m_toGenerator;
        if (m_fromGenerator != m_toGenerator) {
            childrenInfo.m_childInfos.setSize(2);
            childrenInfo.m_childInfos[1].m_node       = m_fromGenerator;
            childrenInfo.m_childInfos[0].m_reactivate = childrenInfo.m_anyReactivate;
            childrenInfo.m_childInfos[1].m_reactivate = false;
            if (m_flags & FLAG_SYNC) {
                childrenInfo.m_childInfos[0].m_isActive = true;
                childrenInfo.m_childInfos[1].m_isActive = true;
                childrenInfo.m_anyActive                = true;
            }
        } else {
            childrenInfo.m_childInfos[0].m_reactivate = childrenInfo.m_anyReactivate;
        }

        const EventMode eventMode           = getEventMode();
        const bool      ignoreFromGenerator = (eventMode == EVENT_MODE_IGNORE_FROM_GENERATOR);
        const bool      ignoreToGenerator   = (eventMode == EVENT_MODE_IGNORE_TO_GENERATOR);
        if (ignoreFromGenerator || ignoreToGenerator) {
            childrenInfo.m_anyIgnoreEvents = true;
            if (m_fromGenerator != m_toGenerator) {
                childrenInfo.m_childInfos[0].m_ignoreEvents = ignoreToGenerator;
                childrenInfo.m_childInfos[1].m_ignoreEvents = ignoreFromGenerator;
            } else {
                childrenInfo.m_childInfos[0].m_ignoreEvents = false;
            }
        }
    }

    hkbNode* hkbBlendingTransitionEffect::cloneNode(hkbBehaviorGraph& rootBehavior) const
    {
        return new hkbBlendingTransitionEffect(*this);
    }

    void hkbBlendingTransitionEffect::generate(const hkbContext& context, const hkbGeneratorOutput** activeChildrenOutput, hkbGeneratorOutput& output,
                                               hkReal timeOffset)
    {
        hkbBlendingTransitionEffectUtils::generateInternal(*this, timeOffset, context.m_character->getPoseLocal(), activeChildrenOutput, output);
    }

    bool hkbBlendingTransitionEffect::canRecycleOutput() const
    {
        return m_fromGenerator == m_toGenerator;
    }

    void hkbBlendingTransitionEffect::updateSync(const hkbContext& context, hkbNodeInfo& info)
    {
        updateSyncInternal(context, this);
    }

    bool hkbBlendingTransitionEffect::isDone()
    {
        return m_timeInTransition > 0.0f && (m_fromGenerator == m_toGenerator || m_timeRemaining <= 0.0f);
    }

    void hkbBlendingTransitionEffect::setFromGenerator(hkbGenerator* fromGen)
    {
        m_fromGenerator = fromGen;
    }

    void hkbBlendingTransitionEffect::setToGenerator(hkbGenerator* toGen)
    {
        m_toGenerator = toGen;
    }

    hkReal hkbBlendingTransitionEffect::getFromGeneratorBlendOutTime()
    {
        return m_duration;
    }

    hkReal hkbBlendingTransitionEffect::getToGeneratorBlendInTime()
    {
        return m_duration;
    }

    /// @note Under FLAG_SYNC the record is the two generators' sync infos blended by the transition's progress, and on
    ///       the first update the to-generator jumps to the matching local time; without it the record reports no
    ///       timeline and the to-generator starts at m_toGeneratorStartTimeFraction of its cycle.
    void hkbBlendingTransitionEffect::updateSyncInternal(const hkbContext& context, hkbNode* syncNode)
    {
        hkbBehaviorGraph* rootBehavior = context.m_character->m_behaviorGraph;
        if (m_applySelfTransition) {
            applySelfTransitionMode(*rootBehavior, m_toGenerator);
            m_applySelfTransition = false;
        }

        hkbNodeInfo& info = rootBehavior->getNodeInfo(syncNode);
        const hkbGeneratorSyncInfo* fromSyncInfo = &info.m_syncInfo;
        hkReal                      duration;
        if (m_fromGenerator == m_toGenerator) {
            hkString::memCpy(&info.m_syncInfo, &rootBehavior->getNodeInfo(m_fromGenerator).m_syncInfo, sizeof(hkbGeneratorSyncInfo));
            info.m_syncInfo.m_isMirrored = false;
            duration                     = getDuration();
        } else if (m_flags & FLAG_SYNC) {
            if (canSyncGenerators(*rootBehavior)) {
                const bool initSync = isAtStart();
                if (useFromGeneratorSyncInfo()) {
                    hkString::memCpy(&info.m_syncInfo, &getFromGeneratorSyncInfo(*rootBehavior), sizeof(hkbGeneratorSyncInfo));
                    info.m_syncInfo.m_isMirrored = false;
                } else {
                    const hkReal totalTime = m_timeRemaining + m_timeInTransition;
                    hkLocalBuffer<const hkbGeneratorSyncInfo*> syncInfos(2);
                    hkLocalBuffer<hkReal>                      weights(2);
                    syncInfos[0] = &getFromGeneratorSyncInfo(*rootBehavior);
                    syncInfos[1] = &rootBehavior->getNodeInfo(m_toGenerator).m_syncInfo;
                    const hkReal toWeight = (totalTime == 0.0f) ? 0.0f : hkMath::min2(m_timeInTransition / totalTime, 1.0f);
                    weights[1]            = toWeight;
                    weights[0]            = 1.0f - toWeight;
                    hkbBlenderGeneratorUtils::computeSyncInfo(&syncInfos[0], 2, &weights[0], 0.0f, initSync, info.m_syncInfo);
                }
                if (initSync) {
                    const hkbNodeInfo& toInfo    = rootBehavior->getNodeInfo(m_toGenerator);
                    const hkReal       localTime = hkbBlenderGeneratorUtils::computeSyncedLocalTime(info.m_syncInfo.m_localTime, info.m_syncInfo, toInfo.m_syncInfo);
                    rootBehavior->setLocalTime(m_toGenerator, localTime);
                    duration = getDuration();
                } else {
                    duration = getDuration();
                }
            } else {
                info.m_syncInfo.m_playbackSpeed = 1.0f;
                info.m_syncInfo.m_numSyncPoints = 0;
                info.m_syncInfo.m_isCyclic      = false;
                info.m_syncInfo.m_isMirrored    = false;
                info.m_syncInfo.m_isAdditive    = false;
                info.m_syncInfo.m_baseFrequency = 0.0f;
                info.m_syncInfo.m_localTime     = 0.0f;
                duration                        = getDuration();
            }
        } else {
            info.m_syncInfo.m_playbackSpeed = 1.0f;
            info.m_syncInfo.m_numSyncPoints = 0;
            info.m_syncInfo.m_isCyclic      = false;
            info.m_syncInfo.m_isMirrored    = false;
            info.m_syncInfo.m_isAdditive    = false;
            info.m_syncInfo.m_baseFrequency = 0.0f;
            info.m_syncInfo.m_localTime     = 0.0f;
            if (isAtStart() && m_toGeneratorStartTimeFraction != 0.0f && rootBehavior->getNodeInfo(m_toGenerator).m_syncInfo.m_baseFrequency != 0.0f) {
                const hkReal period = 1.0f / rootBehavior->getNodeInfo(m_toGenerator).m_syncInfo.m_baseFrequency;
                rootBehavior->setLocalTime(m_toGenerator, period * m_toGeneratorStartTimeFraction);
            }
            fromSyncInfo = &getFromGeneratorSyncInfo(*rootBehavior);
            duration     = getDuration();
        }
        computeTimeRemaining(duration, *fromSyncInfo);
    }

    /// @note `syncNode`'s local time wraps into its cycle (1 / base frequency) by whole cycles.
    void hkbBlendingTransitionEffect::updateInternal(const hkbContext& context, hkReal timestep, hkbNode* syncNode)
    {
        hkbBehaviorGraph* rootBehavior = context.m_character->m_behaviorGraph;
        hkbNodeInfo&      info         = rootBehavior->getNodeInfo(syncNode);
        info.m_syncInfo.m_localTime += timestep;
        if (info.m_syncInfo.m_baseFrequency != 0.0f) {
            const hkReal period = 1.0f / info.m_syncInfo.m_baseFrequency;
            if (info.m_syncInfo.m_localTime > period) {
                info.m_syncInfo.m_localTime -= static_cast<hkReal>(static_cast<int>(info.m_syncInfo.m_localTime / period)) * period;
            }
        }

        m_timeInTransition += timestep;
        m_timeRemaining -= timestep;

        if (m_initializeCharacterPose && m_characterPoseAtBeginningOfTransition.getSize() == 0) {
            const int numBones = context.m_character->m_setup->m_animationSkeleton->m_bones.getSize();
            m_characterPoseAtBeginningOfTransition.reserveExactly(numBones);
            m_characterPoseAtBeginningOfTransition.append(context.m_character->getPoseLocal(), numBones);
        }
    }

    void hkbBlendingTransitionEffect::computeTimeRemaining(hkReal duration, const hkbGeneratorSyncInfo& fromSyncInfo)
    {
        switch (m_endMode) {
            case END_MODE_NONE:
                m_timeRemaining = duration - m_timeInTransition;
                break;
            case END_MODE_TRANSITION_UNTIL_END_OF_FROM_GENERATOR:
                if (fromSyncInfo.m_baseFrequency != 0.0f) {
                    m_timeRemaining = 1.0f / fromSyncInfo.m_baseFrequency - fromSyncInfo.m_localTime;
                } else {
                    m_timeRemaining = 0.0f;
                }
                break;
            case END_MODE_CAP_DURATION_AT_END_OF_FROM_GENERATOR:
                if (fromSyncInfo.m_baseFrequency != 0.0f) {
                    m_timeRemaining = duration - m_timeInTransition;
                    m_timeRemaining = hkMath::min2(1.0f / fromSyncInfo.m_baseFrequency - fromSyncInfo.m_localTime, m_timeRemaining);
                } else {
                    m_timeRemaining = 0.0f;
                }
                break;
        }
    }

    bool hkbBlendingTransitionEffect::canSyncGenerators(hkbBehaviorGraph& rootBehavior)
    {
        return rootBehavior.getNodeInfo(m_fromGenerator).m_syncInfo.m_baseFrequency != 0.0f &&
               rootBehavior.getNodeInfo(m_toGenerator).m_syncInfo.m_baseFrequency != 0.0f;
    }

    bool hkbBlendingTransitionEffect::isAtStart()
    {
        return m_timeInTransition == 0.0f;
    }

    bool hkbBlendingTransitionEffect::useFromGeneratorSyncInfo()
    {
        return false;
    }

    const hkbGeneratorSyncInfo& hkbBlendingTransitionEffect::getFromGeneratorSyncInfo(hkbBehaviorGraph& rootBehavior)
    {
        return rootBehavior.getNodeInfo(m_fromGenerator).m_syncInfo;
    }

    hkReal hkbBlendingTransitionEffect::getDuration()
    {
        return m_duration;
    }

    bool hkbBlendingTransitionEffect::isStarting()
    {
        return isAtStart();
    }

    hkReferencedObject* hkbBlendingTransitionEffect::createInternalState()
    {
        return new hkbBlendingTransitionEffectInternalState;
    }

    void hkbBlendingTransitionEffect::getInternalState(hkReferencedObject& _internalState) const
    {
        hkbBlendingTransitionEffectInternalState& internalState = static_cast<hkbBlendingTransitionEffectInternalState&>(_internalState);

        internalState.m_characterPoseAtBeginningOfTransition = m_characterPoseAtBeginningOfTransition;
        internalState.m_timeRemaining                        = m_timeRemaining;
        internalState.m_timeInTransition                     = m_timeInTransition;
        internalState.m_applySelfTransition                  = m_applySelfTransition;
        internalState.m_initializeCharacterPose              = m_initializeCharacterPose;
    }

    void hkbBlendingTransitionEffect::setInternalState(const hkReferencedObject& _internalState)
    {
        const hkbBlendingTransitionEffectInternalState& internalState =
            static_cast<const hkbBlendingTransitionEffectInternalState&>(_internalState);

        m_characterPoseAtBeginningOfTransition = internalState.m_characterPoseAtBeginningOfTransition;
        m_timeRemaining                        = internalState.m_timeRemaining;
        m_timeInTransition                     = internalState.m_timeInTransition;
        m_applySelfTransition                  = internalState.m_applySelfTransition;
        m_initializeCharacterPose              = internalState.m_initializeCharacterPose;
    }
} }
