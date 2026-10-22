/// @file
/// @brief hkbGeneratorTransitionEffect, reimplemented against vanilla (AE 1.6.1170).
/// @note Vanilla's unit runs 0x140b02310-0x140b02e2a (the exe names the file TransitionEffect\Generator\
///       hkbGeneratorTransitionEffect.cpp in the constructor's warning).
#include <Behavior/TransitionEffect/Generator/hkbGeneratorTransitionEffect.h>
#include <Behavior/TransitionEffect/Generator/hkbGeneratorTransitionEffectUtils.h>

#include <Behavior/BehaviorGraph/hkbBehaviorGraph.h>
#include <Behavior/Context/hkbContext.h>
#include <Behavior/Project/hkbProjectData.h>

namespace CB { namespace havok
{
    HK_FORCE_INLINE void hkbGeneratorTransitionEffect::updateStage()
    {
        if (m_duration == 0.0f) {
            if (m_effectiveBlendInDuration == 0.0f) {
                m_stage = STAGE_PLAYING_TRANSITION_GENERATOR;
            } else {
                m_stage = STAGE_BLENDING_IN;
            }
        } else {
            hkReal blendInDuration;
            hkReal blendOutDuration;
            hkbGeneratorTransitionEffectUtils::computeBlendDurations(*this, blendInDuration, blendOutDuration);

            hkEnum<Stage, hkInt8> stage;
            if (m_timeInTransition < blendInDuration) {
                stage = STAGE_BLENDING_IN;
            } else {
                stage = (blendOutDuration > m_duration - m_timeInTransition) ? STAGE_BLENDING_OUT : STAGE_PLAYING_TRANSITION_GENERATOR;
            }
            m_stage = hkMath::max2(m_stage, stage);
        }
    }

    hkbGeneratorTransitionEffect::hkbGeneratorTransitionEffect()
    :   m_blendInDuration(0.0f),
        m_blendOutDuration(0.0f),
        m_syncToGeneratorStartTime(true),
        m_fromGenerator(HK_NULL),
        m_toGenerator(HK_NULL),
        m_timeInTransition(0.0f),
        m_duration(0.0f),
        m_effectiveBlendInDuration(0.0f),
        m_effectiveBlendOutDuration(0.0f),
        m_toGeneratorState(STATE_INACTIVE),
        m_echoTransitionGenerator(false),
        m_echoToGenerator(false),
        m_justActivated(false),
        m_updateActiveNodes(false),
        m_stage(STAGE_BLENDING_IN)
    {
        hkStringBuf buf;
        buf.printf("Transition effect generators are not supported by our Bethesda Game Studios, please switch to blending "
                   "transitions with a state in the middle.");
        hkError::getInstance().message(hkError::MESSAGE_WARNING, 0x0, buf.cString(), "TransitionEffect\\Generator\\hkbGeneratorTransitionEffect.cpp", 0x58);
    }

    hkbGeneratorTransitionEffect::hkbGeneratorTransitionEffect(const hkbGeneratorTransitionEffect& te)
    :   hkbTransitionEffect(te),
        m_transitionGenerator(te.m_transitionGenerator),
        m_blendInDuration(te.m_blendInDuration),
        m_blendOutDuration(te.m_blendOutDuration),
        m_syncToGeneratorStartTime(te.m_syncToGeneratorStartTime),
        m_fromGenerator(HK_NULL),
        m_toGenerator(HK_NULL),
        m_timeInTransition(0.0f),
        m_duration(0.0f),
        m_effectiveBlendInDuration(0.0f),
        m_effectiveBlendOutDuration(0.0f),
        m_toGeneratorState(STATE_INACTIVE),
        m_echoTransitionGenerator(false),
        m_echoToGenerator(false),
        m_justActivated(false),
        m_updateActiveNodes(false),
        m_stage(STAGE_BLENDING_IN)
    {
    }

    /// @note m_duration is 0 here (vanilla re-reads it after the isNodeActive calls), so activate always takes the first
    ///       branch of updateStage.
    void hkbGeneratorTransitionEffect::activate(const hkbContext& context)
    {
        m_timeInTransition        = 0.0f;
        m_duration                = 0.0f;
        m_toGeneratorState        = STATE_INACTIVE;
        m_justActivated           = true;
        m_echoTransitionGenerator = context.isNodeActive(m_transitionGenerator);
        m_echoToGenerator         = context.isNodeActive(m_toGenerator);
        m_defaultEventMode        = context.m_character->m_projectData->m_defaultEventMode;
        m_effectiveBlendInDuration  = m_blendInDuration;
        m_effectiveBlendOutDuration = m_blendOutDuration;
        updateStage();
    }

    void hkbGeneratorTransitionEffect::update(const hkbContext& context, hkReal timestep)
    {
        m_timeInTransition += timestep;
        const hkEnum<Stage, hkInt8> previousStage = m_stage;
        updateToGeneratorState(*context.m_character->m_behaviorGraph);
        updateStage();

        hkbBehaviorGraph* rootBehavior   = context.m_character->m_behaviorGraph;
        rootBehavior->m_updateActiveNodes = (m_stage != previousStage) | m_updateActiveNodes | rootBehavior->m_updateActiveNodes;
        m_updateActiveNodes               = false;
    }

    void hkbGeneratorTransitionEffect::deactivate(const hkbContext& context)
    {
        m_timeInTransition = m_duration;
    }

    int hkbGeneratorTransitionEffect::getMaxNumChildren(GetChildrenFlags flags)
    {
        return 3;
    }

    void hkbGeneratorTransitionEffect::getChildren(GetChildrenFlags flags, ChildrenInfo& childrenInfo)
    {
        if (m_stage != STAGE_PLAYING_TRANSITION_GENERATOR && (flags & FLAG_ACTIVE_ONLY)) {
            childrenInfo.m_childInfos.setSize(2);
            const bool ignoreFromGenerator = (getEventMode() == EVENT_MODE_IGNORE_FROM_GENERATOR);
            if (m_stage == STAGE_BLENDING_IN) {
                childrenInfo.m_anyIgnoreEvents               = ignoreFromGenerator;
                childrenInfo.m_childInfos[0].m_node         = m_fromGenerator;
                childrenInfo.m_childInfos[1].m_node         = m_transitionGenerator;
                childrenInfo.m_childInfos[0].m_ignoreEvents = ignoreFromGenerator;
                childrenInfo.m_childInfos[1].m_ignoreEvents = false;
                childrenInfo.m_childInfos[0].m_skipGenerate = false;
                childrenInfo.m_childInfos[1].m_skipGenerate = false;
            } else {
                const bool ignoreToGenerator   = (getEventMode() == EVENT_MODE_IGNORE_TO_GENERATOR);
                childrenInfo.m_anyIgnoreEvents = ignoreFromGenerator || ignoreToGenerator;
                if (m_toGeneratorState == STATE_READY_FOR_APPLY_SELF_TRANSITION_MODE && m_selfTransitionMode == SELF_TRANSITION_MODE_RESET) {
                    childrenInfo.m_anyReactivate = m_echoToGenerator;
                    m_toGeneratorState           = STATE_ACTIVE;
                }
                childrenInfo.m_childInfos[0].m_node         = m_toGenerator;
                childrenInfo.m_childInfos[1].m_node         = m_transitionGenerator;
                childrenInfo.m_childInfos[0].m_reactivate   = childrenInfo.m_anyReactivate;
                childrenInfo.m_childInfos[1].m_reactivate   = false;
                childrenInfo.m_childInfos[0].m_ignoreEvents = ignoreToGenerator;
                childrenInfo.m_childInfos[1].m_ignoreEvents = ignoreFromGenerator;
                return;
            }
        } else {
            childrenInfo.m_childInfos.setSize(1);
            childrenInfo.m_childInfos[0].m_node = m_transitionGenerator;
            if (!(flags & FLAG_ACTIVE_ONLY)) return;
            childrenInfo.m_childInfos[0].m_skipGenerate = false;
        }

        childrenInfo.m_anySkipGenerate = true;
        const int numChildren          = childrenInfo.m_childInfos.getSize();
        childrenInfo.m_childInfos.setSize(numChildren + 1);
        childrenInfo.m_childInfos[numChildren].m_node         = m_toGenerator;
        childrenInfo.m_childInfos[numChildren].m_skipGenerate = true;
        childrenInfo.m_childInfos[numChildren].m_ignoreEvents = false;

    }

    bool hkbGeneratorTransitionEffect::isValid(hkStringPtr& errorString) const
    {
        if (m_transitionGenerator == HK_NULL) {
            errorString = "A child generator must be provided.";
            return false;
        }
        return true;
    }

    hkbNode* hkbGeneratorTransitionEffect::cloneNode(hkbBehaviorGraph& rootBehavior) const
    {
        return new hkbGeneratorTransitionEffect(*this);
    }

    void hkbGeneratorTransitionEffect::generate(const hkbContext& context, const hkbGeneratorOutput** activeChildrenOutput, hkbGeneratorOutput& output,
                                                hkReal timeOffset)
    {
        hkbGeneratorTransitionEffectUtils::generateInternal(*this, activeChildrenOutput, output);
    }

    /// @note On the first update after activate this echoes the transition generator if it was already playing and
    ///       starts the to-generator's countdown. A cyclic to-generator under m_syncToGeneratorStartTime starts at the
    ///       local time that ends its cycle with the blend-out.
    void hkbGeneratorTransitionEffect::updateSync(const hkbContext& context, hkbNodeInfo& info)
    {
        hkbBehaviorGraph&  rootBehavior   = *context.m_character->m_behaviorGraph;
        const hkbNodeInfo& transitionInfo = rootBehavior.getNodeInfo(m_transitionGenerator);

        if (transitionInfo.m_syncInfo.m_baseFrequency != 0.0f) {
            const hkReal previousDuration = m_duration;
            m_duration                    = 1.0f / transitionInfo.m_syncInfo.m_baseFrequency;
            if (previousDuration != 0.0f && previousDuration != m_duration) {
                const hkReal scale          = m_duration / previousDuration;
                m_timeInTransition          *= scale;
                m_effectiveBlendInDuration  *= scale;
                m_effectiveBlendOutDuration *= scale;
            }
        }

        if (m_justActivated) {
            m_justActivated = false;
            if (m_echoTransitionGenerator) rootBehavior.startEcho(m_duration - transitionInfo.m_syncInfo.m_localTime, m_transitionGenerator);
            updateToGeneratorState(rootBehavior);
        }

        if (m_toGeneratorState == STATE_READY_FOR_SET_LOCAL_TIME) {
            hkReal blendInDuration;
            hkReal blendOutDuration;
            hkbGeneratorTransitionEffectUtils::computeBlendDurations(*this, blendInDuration, blendOutDuration);

            bool started = false;
            if (blendOutDuration > 0.0f) {
                const hkbNodeInfo& toInfo = rootBehavior.getNodeInfo(m_toGenerator);
                if (toInfo.m_syncInfo.m_baseFrequency != 0.0f && toInfo.m_syncInfo.m_isCyclic && m_syncToGeneratorStartTime) {
                    const hkReal localTime = hkMath::max2(1.0f / toInfo.m_syncInfo.m_baseFrequency - blendOutDuration, 0.0f);
                    if (localTime != 0.0f) {
                        rootBehavior.setNodeLocalTime(m_toGenerator, localTime);
                        started = true;
                    }
                }
            }
            if (started) {
                m_toGeneratorState = STATE_ACTIVE;
            } else {
                m_toGeneratorState  = STATE_READY_FOR_APPLY_SELF_TRANSITION_MODE;
                m_updateActiveNodes = true;
            }
        }

        hkString::memCpy(&info.m_syncInfo, &transitionInfo.m_syncInfo, sizeof(hkbGeneratorSyncInfo));
        info.m_syncInfo.m_isMirrored = false;
    }

    bool hkbGeneratorTransitionEffect::isDone()
    {
        return m_timeInTransition > 0.0f && m_duration - m_timeInTransition <= 0.0f && m_toGeneratorState == STATE_ACTIVE;
    }

    void hkbGeneratorTransitionEffect::setFromGenerator(hkbGenerator* fromGen)
    {
        m_fromGenerator = fromGen;
    }

    void hkbGeneratorTransitionEffect::setToGenerator(hkbGenerator* toGen)
    {
        m_toGenerator = toGen;
    }

    hkReal hkbGeneratorTransitionEffect::getFromGeneratorBlendOutTime()
    {
        return m_effectiveBlendInDuration;
    }

    hkReal hkbGeneratorTransitionEffect::getToGeneratorBlendInTime()
    {
        return m_effectiveBlendOutDuration;
    }

    void hkbGeneratorTransitionEffect::updateToGeneratorState(hkbBehaviorGraph& rootBehavior)
    {
        if (m_toGeneratorState == STATE_INACTIVE) {
            hkReal blendInDuration;
            hkReal blendOutDuration;
            hkbGeneratorTransitionEffectUtils::computeBlendDurations(*this, blendInDuration, blendOutDuration);
            if (m_duration - m_timeInTransition < blendOutDuration) {
                m_toGeneratorState  = STATE_READY_FOR_SET_LOCAL_TIME;
                m_updateActiveNodes = true;
            }
        } else if (m_toGeneratorState == STATE_READY_FOR_APPLY_SELF_TRANSITION_MODE && m_selfTransitionMode != SELF_TRANSITION_MODE_RESET) {
            applySelfTransitionMode(rootBehavior, m_toGenerator);
            m_toGeneratorState  = STATE_ACTIVE;
            m_updateActiveNodes = true;
        }
    }

    hkReferencedObject* hkbGeneratorTransitionEffect::createInternalState()
    {
        return new hkbGeneratorTransitionEffectInternalState;
    }

    void hkbGeneratorTransitionEffect::getInternalState(hkReferencedObject& _internalState) const
    {
        hkbGeneratorTransitionEffectInternalState& internalState = static_cast<hkbGeneratorTransitionEffectInternalState&>(_internalState);

        internalState.m_timeInTransition          = m_timeInTransition;
        internalState.m_duration                  = m_duration;
        internalState.m_effectiveBlendInDuration  = m_effectiveBlendInDuration;
        internalState.m_effectiveBlendOutDuration = m_effectiveBlendOutDuration;
        internalState.m_toGeneratorState          = m_toGeneratorState;
        internalState.m_echoTransitionGenerator   = m_echoTransitionGenerator;
        internalState.m_echoToGenerator           = m_echoToGenerator;
        internalState.m_justActivated             = m_justActivated;
        internalState.m_updateActiveNodes         = m_updateActiveNodes;
        internalState.m_stage                     = m_stage;
    }

    void hkbGeneratorTransitionEffect::setInternalState(const hkReferencedObject& _internalState)
    {
        const hkbGeneratorTransitionEffectInternalState& internalState =
            static_cast<const hkbGeneratorTransitionEffectInternalState&>(_internalState);

        m_timeInTransition          = internalState.m_timeInTransition;
        m_duration                  = internalState.m_duration;
        m_effectiveBlendInDuration  = internalState.m_effectiveBlendInDuration;
        m_effectiveBlendOutDuration = internalState.m_effectiveBlendOutDuration;
        m_toGeneratorState          = internalState.m_toGeneratorState;
        m_echoTransitionGenerator   = internalState.m_echoTransitionGenerator;
        m_echoToGenerator           = internalState.m_echoToGenerator;
        m_justActivated             = internalState.m_justActivated;
        m_updateActiveNodes         = internalState.m_updateActiveNodes;
        m_stage                     = internalState.m_stage;
    }
} }
