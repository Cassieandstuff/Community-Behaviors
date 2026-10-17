/// @file
/// @brief hkbClipGenerator, reimplemented against vanilla (AE 1.6.1170); tools/cbmatch/manifests/schema.yaml checks
///        every function here against the exe.
/// @note Spelling matters: the compiler's output follows the source's shape (statement order, comparison operand
///       order, ternary vs if, which expressions are their own statement). Where one form matched vanilla and an
///       equivalent did not, the definition says so; keep those forms.
#include <Behavior/Generator/Clip/hkbClipGenerator.h>
#include <Behavior/Generator/hkbGeneratorOutputUtils.h>

#include <Animation/Rig/hkaSkeleton.h>
#include <Common/Base/Memory/Util/Stats/hkStatisticsCollector.h>

namespace CB { namespace schema
{
    namespace
    {
        /// @brief Where a trigger sits in a clip of `duration`: an acyclic trigger's own time, otherwise its time (from
        ///        the end when relativeToEndOfClip) clamped into [0, duration].
        /// @note Name provisional.
        HK_FORCE_INLINE hkReal resolveTriggerTime(const hkbClipTrigger& trig, hkReal duration)
        {
            if (trig.m_acyclic.m_bool) return trig.m_localTime;
            const hkReal t = trig.m_relativeToEndOfClip.m_bool ? duration + trig.m_localTime : trig.m_localTime;
            return hkMath::min2(hkMath::max2(t, 0.0f), duration);
        }
    }

    /// @note Defined in the clip's translation unit, as in vanilla (its first function by address). A fresh object:
    ///       the reference count starts at 1, the copy is only of the triggers.
    hkbClipTriggerArray::hkbClipTriggerArray(const hkbClipTriggerArray& other)
    :   hkReferencedObject()
    {
        m_triggers = other.m_triggers;
    }

    /// @note The two warnings pass vanilla's own file string and line numbers (0x10d, 0x122).
    void hkbClipGenerator::activate(const hkbContext& context)
    {
        HK_TIMER_BEGIN("hkbClipGenerator::activate");

        if (m_userData == 0 && g_animationClipLoader) g_animationClipLoader->queue(context, this, HK_NULL);

        if (m_userData == 8) {
            m_animationControl.setAndDontIncrementRefCount(new hkaDefaultAnimationControl(HK_NULL, true, 2));
        } else {
            if (m_animationBindingIndex != -1) {
                m_originalTriggers = m_triggers;

                hkbAnimationBindingSet*          bindingSet = context.m_character->getAnimationBindingSet();
                hkbAnimationBindingWithTriggers* bwt =
                    (m_animationBindingIndex < bindingSet->m_bindings.getSize()) ? bindingSet->m_bindings[m_animationBindingIndex] : HK_NULL;
                hkaAnimationBinding* binding = bwt ? bwt->m_binding : HK_NULL;

                if (m_animationControl) {
                    m_animationControl->setBinding(binding);
                } else {
                    m_animationControl.setAndDontIncrementRefCount(
                        new hkaDefaultAnimationControl((binding && binding->m_animation) ? binding : HK_NULL, true, 2));
                }

                if (!(m_flags & FLAG_DONT_MERGE_ANNOTATIONS)) {
                    const int numTriggers = bwt ? bwt->m_triggers.getSize() : 0;
                    if (numTriggers > 0) {
                        if (m_triggers) m_triggers.setAndDontIncrementRefCount(new hkbClipTriggerArray(*m_triggers));
                        else            m_triggers.setAndDontIncrementRefCount(new hkbClipTriggerArray());

                        m_triggers->m_triggers.reserve(m_triggers->m_triggers.getSize() + numTriggers);

                        const hkReal duration = computeUsableDuration();
                        for (int i = 0; i < numTriggers; ++i) {
                            const hkbAnimationBindingWithTriggers::Trigger& trig = bwt->m_triggers[i];
                            const hkReal                                     t    = trig.m_time - m_cropStartAmountLocalTime;
                            if (t >= 0.0f && t <= duration) {
                                const int        externalId = trig.m_event.getId();
                                hkbEventPayload* payload    = trig.m_event.getPayload();
                                hkbSymbolIdMap*  map        = context.m_behavior->m_eventIDMap;
                                const int        internalId = map ? map->getInternal(externalId) : externalId;

                                if (internalId == -1) {
                                    hkStringBuf                       buf;
                                    hkbBehaviorGraph*                 graph = context.m_character->m_behaviorGraph;
                                    const hkbBehaviorGraphStringData* sd    = (graph && graph->m_data) ? graph->m_data->m_stringData.val() : HK_NULL;
                                    const int                         n     = sd ? sd->m_eventNames.getSize() : 0;
                                    buf.printf("%s: Could not find event %d (%s) in graph %s ", m_name.cString(), externalId,
                                               (externalId >= 0 && externalId < n) ? sd->m_eventNames[externalId].cString() : "unknown",
                                               context.m_behavior ? context.m_behavior->m_name.cString() : "");
                                    hkError::getInstance().message(hkError::MESSAGE_WARNING, 0x0, buf.cString(), "Generator\\Clip\\hkbClipGenerator.cpp", 0x10d);
                                } else {
                                    addTrigger(t, hkbEvent(internalId, payload, HK_NULL), false, false, true);
                                }
                            }
                        }
                    }
                }
            }

            if (!m_animationControl->m_binding) {
                hkStringBuf       buf;
                hkbBehaviorGraph* graph = context.m_character->m_behaviorGraph;
                buf.printf("%s's %s: Animation control exists but binding was not created.  Was the animation loaded and connected to the control?",
                           graph ? graph->m_name.cString() : "", m_name.cString());
                hkError::getInstance().message(hkError::MESSAGE_WARNING, 0xa781b89f, buf.cString(), "Generator\\Clip\\hkbClipGenerator.cpp", 0x122);
            }

            m_binding = const_cast<hkaAnimationBinding*>(m_animationControl->getAnimationBinding());

            hkaAnimationBinding* binding = m_binding;
            hkbCharacterSetup*   setup   = context.m_character->m_setup;
            if (binding && binding->m_originalSkeletonName.cString() && setup->m_animationSkeleton &&
                hkString::strCmp(setup->m_animationSkeleton->m_name.cString(), binding->m_originalSkeletonName.cString()) != 0) {
                const hkaSkeletonMapper* mapper = setup->getRetargetingSkeletonMapper(binding->m_originalSkeletonName.cString());
                if (mapper) m_mapperData = new hkaDefaultAnimationControlMapperData(mapper, binding);
            }

            resetAtEnd();
        }

        HK_TIMER_END();
    }

    /// @note `startTime` is a copy of prevLocalTime so it stays in a register across the calls, as in vanilla.
    void hkbClipGenerator::update(const hkbContext& context, hkReal timestep)
    {
        HK_TIMER_BEGIN("hkbClipGenerator::update");

        if (m_userData == 8 && (!g_animationClipLoader || g_animationClipLoader->load(context, this, HK_NULL))) activate(context);

        if (hkMath::fabs(m_playbackSpeed) < 0.001f) m_playbackSpeed = (m_playbackSpeed > 0.0f) ? 0.001f : -0.001f;

        m_animationControl->m_localTime = m_localTime;

        if (m_atEnd && m_mode != MODE_SINGLE_PLAY) resetAtEnd();

        if (m_mode == MODE_USER_CONTROLLED) {
            if (m_userControlledTimeFraction < 0.0f)      m_userControlledTimeFraction = 0.0f;
            else if (m_userControlledTimeFraction > 1.0f) m_userControlledTimeFraction = 1.0f;
        }

        fn_140ad11f0();

        hkReal prevLocalTime;
        hkReal localTime;
        int    loops;
        bool   atEnd;
        advanceLocalTime(timestep, prevLocalTime, localTime, loops, atEnd);

        if (!m_atEnd) {
            m_localTime                     = localTime;
            m_animationControl->m_localTime = localTime;
        }

        const hkReal  startTime = prevLocalTime;
        hkQsTransform motion;
        getExtractedMotionDelta(context, startTime, localTime, loops, motion);
        m_extractedMotion = motion;

        if (!m_atEnd) {
            m_atEnd = atEnd;
            processTriggers(startTime, localTime, loops, context.getEventQueue(), context);
            if (loops & 1) {
                if (m_mode != MODE_USER_CONTROLLED) m_pingPongBackward = !m_pingPongBackward;
            }
        }

        if (m_mode != MODE_USER_CONTROLLED) {
            const hkReal dt = timestep * m_animationControl->m_playbackSpeed;
            processAcyclicTriggers(m_time, m_time + dt, context.getEventQueue(), context);
            m_time += dt;

            for (int i = 0; i < m_echos.getSize(); i++) {
                Echo& echo = m_echos[i];
                echo.m_weight += timestep * echo.m_dwdt;
                if (echo.m_weight <= 0.0f) {
                    m_echos.removeAtAndCopy(i);
                    i--;
                }
            }
        }

        m_previousUserControlledTimeFraction = m_userControlledTimeFraction;

        HK_TIMER_END();
    }

    void hkbClipGenerator::deactivate(const hkbContext& context)
    {
        fn_140ad11e0();

        m_echos.setSize(0);

        if (m_mapperData) m_mapperData->removeReference();
        m_mapperData = HK_NULL;

        if (m_animationBindingIndex != -1) {
            m_triggers         = m_originalTriggers;
            m_originalTriggers = HK_NULL;
        }

        if (m_mirroredAnimation) {
            m_mirroredAnimation->removeReference();
            m_mirroredAnimation = HK_NULL;
        }

        m_binding = HK_NULL;
        if (m_animationControl) m_animationControl->setBinding(HK_NULL);

        if (m_userData != 0 && g_animationClipLoader) g_animationClipLoader->unload(context, this, HK_NULL);
    }

    void hkbClipGenerator::generate(const hkbContext& context, const hkbGeneratorOutput** activeChildrenOutput, hkbGeneratorOutput& output,
                                    hkReal timeOffset)
    {
        m_animationControl->m_localTime = m_localTime;

        hkQsTransform motion;
        if (timeOffset > 0.0f) {
            hkReal prevLocalTime;
            hkReal localTime;
            int    loops;
            bool   atEnd;
            advanceLocalTime(timeOffset, prevLocalTime, localTime, loops, atEnd);
            hkQsTransform delta;
            getExtractedMotionDelta(context, prevLocalTime, localTime, loops, delta);
            motion = delta;
        } else {
            motion = m_extractedMotion;
        }

        static_cast<hkQsTransform*>(output.getTrackData(hkbGeneratorOutput::TRACK_WORLD_FROM_MODEL))->setMul(*context.m_character->m_worldFromModel, motion);

        if (output.getNumPoseLocal() != 0) {
            HK_TIMER_BEGIN("hkbClipGenerator::generate");

            const int                  numPoseLocal = output.getNumPoseLocal();
            const hkaAnimationBinding* binding      = m_animationControl->m_binding;
            const hkBool               additive     = binding ? (binding->m_blendHint == hkaAnimationBinding::ADDITIVE) : false;
            const bool                 isAdditive   = additive;

            hkReal* floatSlots    = HK_NULL;
            int     numFloatSlots = 0;
            if (output.isValid(hkbGeneratorOutput::TRACK_FLOAT_SLOTS) && !m_mapperData) {
                hkbGeneratorOutput::TrackHeader& header = output.getTrackHeader(hkbGeneratorOutput::TRACK_FLOAT_SLOTS);
                numFloatSlots                           = header.m_numData;
                floatSlots                              = static_cast<hkReal*>(output.getTrackData(hkbGeneratorOutput::TRACK_FLOAT_SLOTS));
                if (numFloatSlots > 0) {
                    hkString::memSet16(floatSlots, isAdditive ? &g_vectorConstantZero : &g_vectorConstantMax,
                                       static_cast<int>((numFloatSlots * sizeof(hkReal) + 15) >> 4));
                    output.getTrackHeader(hkbGeneratorOutput::TRACK_FLOAT_SLOTS).m_onFraction = 1.0f;
                }
            }

            hkBitField partitionsSampled(numPoseLocal + 1, reinterpret_cast<hkUint32*>(output.accessPoseLocal() + numPoseLocal));
            sampleAnimation(context, output.accessPoseLocal(), numPoseLocal, floatSlots, numFloatSlots, context.m_animationCache, partitionsSampled);

            const hkReal   scale = context.m_character->m_setup->m_data->m_scale;
            hkQsTransform* pose  = output.accessPoseLocal();
            hkaSkeletonUtils::scalePose(scale, pose, output.getNumPoseLocal());
            if (!isAdditive) pose[0].m_scale.mul4(hkSimdReal(scale));

            if (m_flags & FLAG_MIRROR_POSE)
                hkbGeneratorOutputUtils::makePoseDense(output, isAdditive ? HK_NULL : context.m_character->m_setup->m_animationSkeleton->m_referencePose.m_data);

            output.getTrackHeader(hkbGeneratorOutput::TRACK_POSE).m_onFraction = 1.0f;
            if (isAdditive) output.getTrackHeader(hkbGeneratorOutput::TRACK_POSE).m_flags |= hkbGeneratorOutput::TRACK_FLAG_ADDITIVE_POSE;

            HK_TIMER_END();
        }

        output.getTrackHeader(hkbGeneratorOutput::TRACK_WORLD_FROM_MODEL).m_onFraction = 1.0f;
        if (output.isValid(hkbGeneratorOutput::TRACK_EXTRACTED_MOTION)) {
            *static_cast<hkQsTransform*>(output.getTrackData(hkbGeneratorOutput::TRACK_EXTRACTED_MOTION)) = motion;
            output.getTrackHeader(hkbGeneratorOutput::TRACK_EXTRACTED_MOTION).m_onFraction = 1.0f;
        }
    }

    /// @note Sync-point times scale by 1/|m_playbackSpeed| (the setting), the local time and frequency by the effective
    ///       speed. Vanilla hoists `graphData != 0` out of the loop as a bool (`test; divss; setnz`) where VS2015 24247
    ///       re-tests the pointer each pass; no spelling tried (bool/hkBool local, `continue`, ternary) reproduces it.
    void hkbClipGenerator::updateSync(const hkbContext& context, hkbNodeInfo& info)
    {
        HK_TIMER_BEGIN("hkbClipGenerator::updateSync");

        hkbGeneratorSyncInfo& syncInfo =
            info.m_isProxy ? (*context.m_character->m_behaviorGraph->m_activeNodes)[info.m_proxyIndex].m_syncInfo : info.m_syncInfo;
        syncInfo.m_numSyncPoints = 0;
        syncInfo.m_playbackSpeed = 1.0f;
        syncInfo.m_isCyclic      = (m_mode == MODE_LOOPING);

        hkReal speed;
        if (m_userData == 8 || (speed = computeEffectivePlaybackSpeed()) == 0.0f) {
            syncInfo.m_baseFrequency = 0.0f;
        } else {
            const hkReal absSpeed  = hkMath::fabs(speed);
            const hkReal duration  = computeUsableDuration();
            const hkReal invSpeed  = 1.0f / absSpeed;
            hkReal       period    = invSpeed * duration;
            hkReal       localTime = (m_localTime - m_animationControl->m_cropStartAmountLocalTime - computeStartTime()) * invSpeed;
            if (localTime < 0.0f) localTime += period;
            if (m_mode == MODE_PING_PONG && !(m_flags & FLAG_SYNC_HALF_CYCLE_IN_PING_PONG_MODE)) period *= 2.0f;
            if (speed < 0.0f) localTime = period - localTime;
            localTime                = (localTime < period) ? localTime : period;
            syncInfo.m_localTime     = localTime;
            syncInfo.m_baseFrequency = (period > 0.0f) ? 1.0f / period : 0.0f;

            if (m_triggers) {
                const int                   numTriggers      = m_triggers->m_triggers.getSize();
                const hkbBehaviorGraphData* graphData        = context.m_behavior->m_data;
                const hkbSymbolIdMap*       eventIDMap       = context.m_behavior->m_eventIDMap;
                const hkReal                invPlaybackSpeed = 1.0f / hkMath::fabs(m_playbackSpeed);
                for (int i = 0; i < numTriggers; i++) {
                    if (syncInfo.m_numSyncPoints >= hkbGeneratorSyncInfo::MAX_SYNC_POINTS) break;

                    const hkbClipTrigger& trig = m_triggers->m_triggers[i];
                    const int             id   = trig.m_event.getId();
                    if (graphData && graphData->isEventSyncPoint(id)) {
                        hkbGeneratorSyncInfo::SyncPoint& point = syncInfo.m_syncPoints[syncInfo.m_numSyncPoints];
                        point.m_id = eventIDMap ? eventIDMap->getExternal(id) : id;

                        hkReal t;
                        if (trig.m_acyclic.m_bool) {
                            t = trig.m_localTime;
                        } else {
                            t = trig.m_relativeToEndOfClip.m_bool ? duration + trig.m_localTime : trig.m_localTime;
                            t = hkMath::min2(hkMath::max2(t, 0.0f), duration);
                        }
                        point.m_time = t * invPlaybackSpeed;
                        syncInfo.m_numSyncPoints++;
                    }
                }
            }
        }

        syncInfo.m_isMirrored              = false;
        const hkaAnimationBinding* binding = m_animationControl->m_binding;
        syncInfo.m_isAdditive              = binding ? (binding->m_blendHint == hkaAnimationBinding::ADDITIVE) : false;
        if (m_flags & FLAG_MIRROR) syncInfo.mirror(context);

        HK_TIMER_END();
    }

    const void* hkbClipGenerator::getClassType() const
    {
        return &staticClass();
    }

    bool hkbClipGenerator::isValid(hkStringPtr& errorString) const
    {
        if (!m_animationControl && m_animationBindingIndex == -1) {
            errorString = "The animation has not been loaded.";
            return false;
        }
        if (m_mode < MODE_SINGLE_PLAY || m_mode > 4) {
            errorString = "The mode is invalid.";
            return false;
        }
        const hkReal duration = m_animationControl->m_binding->m_animation->m_duration - (m_cropEndAmountLocalTime + m_cropStartAmountLocalTime);
        if (duration <= 0.0f) {
            errorString = "The clip is cropped to a duration of zero.";
            return false;
        }
        return true;
    }

    hkbClipGenerator::~hkbClipGenerator()
    {
        if (m_mirroredAnimation) m_mirroredAnimation->removeReference();
    }

    /// @note The clamp is a ternary: `if (t <= 0) t = 0` compiles to maxss, vanilla branches. VS2015 24247 sinks the
    ///       `|speed| × elapsed` product below the computeStartTime call (vanilla computes it before), which costs a
    ///       third saved register — build variance, VARIANT.
    void hkbClipGenerator::setElapsedTime(hkReal elapsed)
    {
        m_animationControl->setCropStartAmountLocalTime(m_cropStartAmountLocalTime);
        m_animationControl->setCropEndAmountLocalTime(m_cropEndAmountLocalTime);

        hkReal t = hkMath::fabs(computeEffectivePlaybackSpeed()) * elapsed;
        t += computeStartTime();
        const bool halfCycle = m_mode == MODE_PING_PONG && !(m_flags & FLAG_SYNC_HALF_CYCLE_IN_PING_PONG_MODE);
        m_pingPongBackward   = false;

        hkReal cycle = computeUsableDuration();
        if (halfCycle) cycle *= 2.0f;
        if (t > cycle) t -= hkMath::floor(t / cycle) * cycle;
        t = (t > 0.0f) ? t : 0.0f;
        if (halfCycle && t > cycle * 0.5f) {
            m_pingPongBackward = true;
            t                  = cycle - t;
        }
        if (m_playbackSpeed < 0.0f) t = cycle - t;

        m_localTime                     = t + m_animationControl->m_cropStartAmountLocalTime;
        m_animationControl->m_localTime = m_localTime;
        m_ignoreStartTime               = true;
    }

    void hkbClipGenerator::startEcho(hkReal echoDuration)
    {
        if (m_mode == MODE_USER_CONTROLLED) return;

        hkaDefaultAnimationControl* control = m_animationControl;
        hkReal                      offset  = m_localTime - (computeStartTime() + control->m_cropStartAmountLocalTime);
        if (offset < 0.0f) {
            if (control->m_binding && control->m_binding->m_animation) offset += control->m_binding->m_animation->m_duration;
            else                                                         offset = 0.0f;
        }

        fn_140ad11e0();
        resetAtEnd();

        if (echoDuration == 0.0f) {
            m_echos.setSize(0);
            return;
        }

        const int    numEchos    = m_echos.getSize();
        int          index       = -1;
        hkReal       minWeight   = 1.0f;
        hkReal       totalWeight = 0.0f;
        const hkReal duration    = computeUsableDuration();
        for (int i = 0; i < numEchos; i++) {
            Echo&       echo = m_echos[i];
            const hkReal t    = offset + echo.m_offsetLocalTime;
            echo.m_offsetLocalTime = t;
            if (t > duration && m_mode == MODE_LOOPING) echo.m_offsetLocalTime = t - duration;
            totalWeight += echo.m_weight;
            if (m_echos[i].m_weight < minWeight) {
                minWeight = m_echos[i].m_weight;
                index     = i;
            }
        }

        if (numEchos < s_maxEchos || minWeight < 1.0f - totalWeight) {
            if (numEchos < s_maxEchos) {
                m_echos.setSize(numEchos + 1);
                index = numEchos;
            }
            Echo& echo            = m_echos[index];
            echo.m_offsetLocalTime = offset;
            echo.m_dwdt            = -1.0f / echoDuration;
            echo.m_weight          = 1.0f - totalWeight;
        }
    }

    hkbNode* hkbClipGenerator::cloneNode(hkbBehaviorGraph& rootBehavior) const
    {
        return new hkbClipGenerator(*this);
    }

    /// @note `(gen.m_userData == 0) ? binding : HK_NULL` — the inverted `gen.m_userData ? HK_NULL : binding` moves a
    ///       register against vanilla. The 0x1c-byte memCpy copies m_cropStartAmountLocalTime through m_flags.
    hkbClipGenerator::hkbClipGenerator(const hkbClipGenerator& gen)
    :   hkbGenerator(gen),
        m_animationName(gen.m_animationName, false),
        m_mapperData(gen.m_mapperData),
        m_binding(HK_NULL),
        m_mirroredAnimation(HK_NULL),
        m_extractedMotion(hkQsTransform::IDENTITY),
        m_localTime(0.0f),
        m_time(0.0f),
        m_previousUserControlledTimeFraction(0.0f),
        m_bufferSize(0),
        m_echoBufferSize(0),
        m_atEnd(false),
        m_ignoreStartTime(false),
        m_pingPongBackward(false)
    {
        m_userData = 0;
        hkString::memCpy(&m_cropStartAmountLocalTime, &gen.m_cropStartAmountLocalTime, 0x1c);

        if (gen.m_animationControl) {
            m_animationControl.setAndDontIncrementRefCount(
                new hkaDefaultAnimationControl((gen.m_userData == 0) ? gen.m_animationControl->getAnimationBinding() : HK_NULL, true, 2));
            m_binding   = const_cast<hkaAnimationBinding*>(gen.m_animationControl->getAnimationBinding());
            m_localTime = m_animationControl->m_localTime;
        }

        m_triggers = gen.m_triggers;
    }

    hkReal hkbClipGenerator::computeEffectivePlaybackSpeed() const
    {
        const hkReal duration = computeUsableDuration();
        const hkReal scale    = (duration > 0.0f && m_enforcedDuration > 0.0f) ? duration / m_enforcedDuration : 1.0f;
        hkReal       speed    = scale * m_playbackSpeed;
        if (m_mode == MODE_PING_PONG && m_pingPongBackward.m_bool) speed = -speed;
        return speed;
    }

    void hkbClipGenerator::fn_140ad11e0()
    {
        m_ignoreStartTime = false;
    }

    /// @note `m_localTime <= cropStart` — `cropStart >= m_localTime` swaps the comparison's operands against vanilla.
    void hkbClipGenerator::fn_140ad11f0()
    {
        if (m_atEnd.m_bool) {
            const hkReal speed = computeEffectivePlaybackSpeed();
            if (m_localTime <= m_animationControl->m_cropStartAmountLocalTime) m_atEnd = (speed <= 0.0f);
            else                                                               m_atEnd = (speed >= 0.0f);
        }
    }

    hkReal hkbClipGenerator::computeStartTime() const
    {
        const hkReal duration = computeUsableDuration();
        hkReal       start    = m_startTime;
        if (m_enforcedDuration > 0.0f) start = start / m_enforcedDuration * duration;
        return (start < duration) ? start : duration;
    }

    void hkbClipGenerator::resetAtEnd()
    {
        m_animationControl->setCropStartAmountLocalTime(m_cropStartAmountLocalTime);
        m_animationControl->setCropEndAmountLocalTime(m_cropEndAmountLocalTime);

        if (m_mode == MODE_USER_CONTROLLED) {
            m_localTime                     = computeUsableDuration() * m_userControlledTimeFraction + m_cropStartAmountLocalTime;
            m_animationControl->m_localTime = m_localTime;
        } else {
            if (!m_ignoreStartTime.m_bool) {
                m_localTime                     = computeStartTime() + m_cropStartAmountLocalTime;
                m_animationControl->m_localTime = m_localTime;
            }
            hkaDefaultAnimationControl* control   = m_animationControl;
            const hkReal                cropStart = control->m_cropStartAmountLocalTime;
            hkReal                      t         = m_localTime - cropStart;
            const hkReal                duration  = computeUsableDuration();
            if (m_playbackSpeed < 0.0f) {
                if (t == 0.0f) t = duration;
            } else if (t == duration) {
                t = 0.0f;
            }
            m_localTime          = cropStart + t;
            control->m_localTime = m_localTime;
        }

        m_atEnd = false;
        m_extractedMotion.setIdentity();
        m_pingPongBackward                   = false;
        m_previousUserControlledTimeFraction = m_userControlledTimeFraction;
        m_time                               = 0.0f;
        sortTriggers();
    }

    /// @note `times[j] > times[j + 1]` — `times[j + 1] < times[j]` swaps the comparison's operands against vanilla.
    /// @note 24247 reuses the buffer's rounded byte count at the free where vanilla recomputes it from the count, which
    ///       moves the spill slots (rule A).
    void hkbClipGenerator::sortTriggers()
    {
        if (m_triggers) {
            const int numTriggers = m_triggers->m_triggers.getSize();
            if (numTriggers > 1) {
                const hkReal duration = (m_enforcedDuration > 0.0f) ? m_enforcedDuration : computeUsableDuration();

                hkLocalBuffer<hkReal> times(numTriggers);
                for (int i = 0; i < numTriggers; i++) times[i] = resolveTriggerTime(m_triggers->m_triggers[i], duration);

                bool swapped = true;
                for (int i = 0; i < numTriggers - 1 && swapped; i++) {
                    swapped = false;
                    for (int j = 0; j < numTriggers - 1 - i; j++) {
                        if (times[j] > times[j + 1]) {
                            hkbClipTrigger tmp           = m_triggers->m_triggers[j];
                            m_triggers->m_triggers[j]     = m_triggers->m_triggers[j + 1];
                            m_triggers->m_triggers[j + 1] = tmp;
                            const hkReal t                = times[j];
                            times[j]                      = times[j + 1];
                            times[j + 1]                  = t;
                            swapped                       = true;
                        }
                    }
                }
            }
        }
    }

    /// @note A call result used inside a larger expression goes in its own statement (`duration`), or the following
    ///       m_animationControl load is hoisted above the call. Each clamp branch repeats its own `atEnd`/`loops`
    ///       stores; a shared tail with `else return` lays the blocks out differently.
    void hkbClipGenerator::advanceLocalTime(hkReal timestep, hkReal& prevLocalTime, hkReal& localTime, int& loops, bool& atEnd)
    {
        atEnd = m_atEnd;

        if (m_mode == MODE_USER_CONTROLLED) {
            const hkReal duration = computeUsableDuration();
            prevLocalTime         = duration * m_previousUserControlledTimeFraction + m_cropStartAmountLocalTime;
            localTime             = duration * m_userControlledTimeFraction + m_cropStartAmountLocalTime;
            loops                 = 0;
            return;
        }

        prevLocalTime                       = m_localTime;
        const hkReal speed                  = computeEffectivePlaybackSpeed();
        m_animationControl->m_playbackSpeed = speed;

        if (!m_atEnd.m_bool) {
            hkaDefaultAnimationControl* control = m_animationControl;
            const hkReal                delta   = timestep * control->m_playbackSpeed;
            if (control->m_binding && control->m_binding->m_animation) {
                control->getFutureTime(timestep, localTime, loops);
            } else {
                localTime = control->m_localTime;
                loops     = 0;
            }

            if (m_mode == MODE_SINGLE_PLAY) {
                const hkReal cropStart = m_animationControl->m_cropStartAmountLocalTime;
                const hkReal end       = computeUsableDuration() + cropStart;
                const hkReal unclamped = delta + prevLocalTime;
                if (unclamped >= end) {
                    localTime = end;
                    atEnd     = true;
                    loops     = 0;
                } else if (unclamped <= cropStart) {
                    localTime = cropStart;
                    atEnd     = true;
                    loops     = 0;
                }
            } else if (m_mode == MODE_PING_PONG && (loops & 1)) {
                const hkReal duration = computeUsableDuration();
                localTime             = duration - localTime + m_animationControl->m_cropStartAmountLocalTime * 2.0f;
            }
        } else if (m_flags & FLAG_CONTINUE_MOTION_AT_END) {
            const hkReal duration = computeUsableDuration();
            const hkReal end      = duration + m_animationControl->m_cropStartAmountLocalTime;
            if (speed > 0.0f) {
                localTime          = end;
                const hkReal start = end - timestep;
                prevLocalTime      = (start > 0.0f) ? start : 0.0f;
            } else {
                localTime     = 0.0f;
                prevLocalTime = (end < timestep) ? end : timestep;
            }
            loops = 0;
        } else {
            localTime     = 0.0f;
            prevLocalTime = 0.0f;
            loops         = 0;
        }
    }

    void hkbClipGenerator::addTrigger(hkReal localTime, const hkbEvent& event, bool relativeToEndOfClip, bool acyclic, bool isAnnotation)
    {
        if (!m_triggers) m_triggers.setAndDontIncrementRefCount(new hkbClipTriggerArray());
        m_triggers->m_triggers.pushBack(hkbClipTrigger(localTime, event, relativeToEndOfClip, acyclic, isAnnotation));
    }

    /// @note Restores m_pingPongBackward afterwards: the per-loop flips only steer which way each range is walked.
    void hkbClipGenerator::processTriggers(hkReal prevLocalTime, hkReal localTime, int loops, hkbEventQueue* queue, const hkbContext& context)
    {
        if (!m_triggers || m_triggers->m_triggers.getSize() <= 0) return;

        const hkBool pingPongBackward = m_pingPongBackward;
        if (loops == 0) {
            processTriggersInRange(prevLocalTime, localTime, m_atEnd, queue, context);
        } else {
            const hkReal duration = computeUsableDuration();
            processTriggersInRange(prevLocalTime, (computeEffectivePlaybackSpeed() > 0.0f) ? duration : 0.0f, true, queue, context);
            if (m_mode == MODE_PING_PONG) m_pingPongBackward = !m_pingPongBackward;

            for (int i = 1; i < loops; i++) {
                if (computeEffectivePlaybackSpeed() > 0.0f) processTriggersInRange(0.0f, duration, true, queue, context);
                else                                        processTriggersInRange(duration, 0.0f, true, queue, context);
                if (m_mode == MODE_PING_PONG) m_pingPongBackward = !m_pingPongBackward;
            }

            processTriggersInRange((computeEffectivePlaybackSpeed() > 0.0f) ? 0.0f : duration, localTime, m_atEnd, queue, context);
        }
        m_pingPongBackward = pingPongBackward;
    }

    /// @note The `atEnd` test is spelled `(atEnd && ..) || (!atEnd && ..)` — a ternary inside the && chain materializes
    ///       a bool. The sender is its own statement before the event is built: vanilla reads id/payload after the call.
    void hkbClipGenerator::processTriggersInRange(hkReal fromTime, hkReal toTime, bool atEnd, hkbEventQueue* queue, const hkbContext& context)
    {
        hkReal from = fromTime;
        hkReal to   = toTime;
        if (from > to) {
            from = toTime;
            to   = fromTime;
        }

        hkReal       duration  = computeUsableDuration();
        const hkReal cropStart = m_animationControl->m_cropStartAmountLocalTime;
        from -= cropStart;
        to -= cropStart;
        if (m_enforcedDuration > 0.0f) {
            const hkReal scale = m_enforcedDuration / duration;
            duration           = m_enforcedDuration;
            from *= scale;
            to *= scale;
        }

        if (!m_triggers) return;
        const int                   numTriggers = m_triggers->m_triggers.getSize();
        const hkbBehaviorGraphData* graphData   = context.m_behavior->m_data;
        for (int i = 0; i < numTriggers; i++) {
            const hkbClipTrigger& trig = m_triggers->m_triggers[i];
            if (graphData && graphData->isEventSilent(trig.m_event.getId())) continue;

            const hkReal t        = resolveTriggerTime(trig, duration);
            const hkReal clipTime = (m_cropStartAmountLocalTime + t) - m_cropStartAmountLocalTime;

            if (!trig.m_acyclic.m_bool && from <= clipTime && ((atEnd && clipTime <= to) || (!atEnd && clipTime < to))) {
                hkbNode* sender = context.m_character->m_behaviorGraph->getNodeTemplate(this);
                hkbEvent event(trig.m_event.getId(), trig.m_event.getPayload(), sender);
                event.m_id = queue->toExternal(event.m_id);
                queue->enqueue(event);
            }
        }
    }

    /// @note Without graph data no acyclic trigger fires (unlike processTriggersInRange, which then fires them all).
    void hkbClipGenerator::processAcyclicTriggers(hkReal fromTime, hkReal toTime, hkbEventQueue* queue, const hkbContext& context)
    {
        if (!m_triggers) return;
        const int numTriggers = m_triggers->m_triggers.getSize();
        if (numTriggers <= 0) return;

        const hkbBehaviorGraphData* graphData = context.m_behavior->m_data;
        for (int i = 0; i < numTriggers; i++) {
            const hkbClipTrigger& trig = m_triggers->m_triggers[i];
            if (trig.m_acyclic.m_bool && graphData && !graphData->isEventSilent(trig.m_event.getId()) && fromTime <= trig.m_localTime &&
                toTime > trig.m_localTime) {
                hkbNode* sender = context.m_character->m_behaviorGraph->getNodeTemplate(this);
                hkbEvent event(trig.m_event.getId(), trig.m_event.getPayload(), sender);
                event.m_id = queue->toExternal(event.m_id);
                queue->enqueue(event);
            }
        }
    }

    /// @note The ternary over hkRefPtr builds a temporary; MSVC's flag-tracked cleanup of it is what vanilla shows.
    void hkbClipGenerator::getExtractedMotionDelta(const hkbContext& context, hkReal prevLocalTime, hkReal localTime, int loops, hkQsTransform& out)
    {
        const hkaAnimationBinding* binding   = m_animationControl->m_binding;
        const hkaAnimation*        animation = binding ? binding->m_animation : hkRefPtr<hkaAnimation>();

        if (animation && !(m_flags & FLAG_IGNORE_MOTION) && animation->m_extractedMotion && (!m_atEnd.m_bool || (m_flags & FLAG_CONTINUE_MOTION_AT_END))) {
            binding->m_animation->getExtractedMotionDeltaReferenceFrame(prevLocalTime, localTime, (m_mode == MODE_PING_PONG) ? 0 : loops, out,
                                                                        m_cropStartAmountLocalTime, m_cropEndAmountLocalTime);
            out.m_translation.mul4(hkSimdReal(context.m_character->m_setup->m_data->m_scale));
            if (m_mapperData) {
                const hkQsTransform motion = out;
                m_mapperData->m_mapper->mapExtractedMotion(motion, out);
            }
            if (m_flags & FLAG_MIRROR) context.m_character->m_setup->getMirroredSkeleton()->mirrorExtractedMotion(out);
        } else {
            out.setIdentity();
        }
    }

    void hkbClipGenerator::calcContentStatistics(hkStatisticsCollector* collector, const hkClass* cls) const
    {
        hkStatisticsCollectorUtil::addDeclaredMembers(this, &hkbClipGeneratorClass, collector);
        collector->addArray("Echos", m_echos, hkStatisticsCollector::MEMORY_RUNTIME);
        for (int i = 0; i < m_echos.getSize(); i++) collector->addNormalChunk("Echo", &m_echos[i], sizeof(Echo));
        collector->addReferencedObject("hkDefaultAnimationControl", m_animationControl);
        hkbGenerator::calcContentStatistics(collector, &hkbGeneratorClass);
    }

    void hkbClipGenerator::setAnimationControl(hkaDefaultAnimationControl* animationControl)
    {
        m_animationControl = animationControl;
    }

    void hkbClipGenerator::removeAnnotationTriggers()
    {
        hkbClipTriggerArray* triggers = m_triggers;
        if (triggers) {
            const int numTriggers = triggers->m_triggers.getSize();
            int       numKept     = 0;
            for (int i = 0; i < numTriggers; i++) {
                if (!triggers->m_triggers[i].m_isAnnotation) {
                    if (i != numKept) triggers->m_triggers[numKept] = triggers->m_triggers[i];
                    numKept++;
                }
            }
            triggers->m_triggers.setSize(numKept);
        }
    }

    void hkbClipGenerator::rebindFromBindingSet(const hkbContext& context)
    {
        hkbAnimationBindingSet* bindingSet = context.m_character ? context.m_character->getAnimationBindingSet() : HK_NULL;
        hkaAnimationBinding*    binding    = HK_NULL;
        if (bindingSet && m_animationBindingIndex < bindingSet->m_bindings.getSize()) {
            hkbAnimationBindingWithTriggers* bwt = bindingSet->m_bindings[m_animationBindingIndex];
            if (bwt) binding = bwt->m_binding;
        }
        if (m_animationControl) m_animationControl->setBinding(binding);
        m_binding = binding;
    }

    void hkbClipGenerator::setLocalTime(hkReal localTime)
    {
        m_localTime = localTime;
    }

    hkReal hkbClipGenerator::getLocalTime() const
    {
        return m_localTime;
    }

    hkbClipGenerator::hkbClipGenerator(hkFinishLoadedObjectFlag flag)
    :   hkbGenerator(flag),
        m_animationName(flag),
        m_triggers(flag),
        m_binding(HK_NULL)
    {
        m_userData = 0;
    }

} }
